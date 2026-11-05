#include "core/patch.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace streetpano::core {
namespace {

constexpr std::uint32_t kFullMask = 255U;
constexpr int kBrightnessRingPixels = 48;  // Around the area, where the images should agree.

struct Box {
    double left = std::numeric_limits<double>::infinity();
    double right = -std::numeric_limits<double>::infinity();
    double bottom = std::numeric_limits<double>::infinity();
    double top = -std::numeric_limits<double>::infinity();
};

[[nodiscard]] Box boxOf(std::span<const WallPoint> outline) {
    Box box;
    for (const WallPoint& point : outline) {
        box.left = std::min(box.left, point.arc);
        box.right = std::max(box.right, point.arc);
        box.bottom = std::min(box.bottom, point.height);
        box.top = std::max(box.top, point.height);
    }
    return box;
}

[[nodiscard]] bool sees(const CameraPose& pose, Vec3 point, double focal, int width, int height) {
    const Vec3 inCamera = multiply(pose.worldToCamera, point - pose.center);
    if (inCamera.z <= 0.0) {
        return false;
    }
    return std::abs(focal * inCamera.x / inCamera.z) <= width / 2.0 &&
           std::abs(focal * inCamera.y / inCamera.z) <= height / 2.0;
}

// Softens a mask of one row or column at a time, using a running sum.
void blurLine(std::uint8_t* values, std::size_t stride, int count, int radius,
              std::vector<std::uint8_t>& scratch) {
    if (radius <= 0) {
        return;
    }
    scratch.resize(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        scratch[static_cast<std::size_t>(index)] = values[static_cast<std::size_t>(index) * stride];
    }
    const int window = 2 * radius + 1;
    int sum = 0;
    for (int index = -radius; index <= radius; ++index) {
        if (index >= 0 && index < count) {
            sum += scratch[static_cast<std::size_t>(index)];
        }
    }
    for (int index = 0; index < count; ++index) {
        values[static_cast<std::size_t>(index) * stride] = static_cast<std::uint8_t>(sum / window);
        const int leaving = index - radius;
        const int entering = index + radius + 1;
        if (leaving >= 0) {
            sum -= scratch[static_cast<std::size_t>(leaving)];
        }
        if (entering < count) {
            sum += scratch[static_cast<std::size_t>(entering)];
        }
    }
}

}  // namespace

std::vector<std::size_t> patchChoices(const FacadeSurface& surface,
                                      std::span<const CameraPose> poses,
                                      std::span<const WallPoint> outline, double focalPixels,
                                      int photoWidth, int photoHeight) {
    if (outline.empty()) {
        return {};
    }
    const Box box = boxOf(outline);
    const double middleArc = (box.left + box.right) / 2.0;
    const double middleHeight = (box.bottom + box.top) / 2.0;
    const Vec3 middle = surface.pointAt(middleArc, middleHeight);
    const std::array<Vec3, 4> corners = {
        surface.pointAt(box.left, box.top), surface.pointAt(box.right, box.top),
        surface.pointAt(box.left, box.bottom), surface.pointAt(box.right, box.bottom)};

    struct Choice {
        std::size_t camera;
        int cornersSeen;
        double distance;
    };
    std::vector<Choice> choices;
    for (std::size_t index = 0; index < poses.size(); ++index) {
        const CameraPose& pose = poses[index];
        if (!sees(pose, middle, focalPixels, photoWidth, photoHeight)) {
            continue;
        }
        int seen = 0;
        for (const Vec3& corner : corners) {
            seen += sees(pose, corner, focalPixels, photoWidth, photoHeight) ? 1 : 0;
        }
        const auto axis = surface.hit(pose.center, pose.forward());
        const double distance = axis.has_value() ? std::abs(axis->arc - middleArc)
                                                 : std::numeric_limits<double>::infinity();
        choices.push_back(Choice{index, seen, distance});
    }
    std::sort(choices.begin(), choices.end(), [](const Choice& a, const Choice& b) {
        return a.cornersSeen != b.cornersSeen ? a.cornersSeen > b.cornersSeen
                                              : a.distance < b.distance;
    });
    std::vector<std::size_t> result;
    result.reserve(choices.size());
    for (const Choice& choice : choices) {
        result.push_back(choice.camera);
    }
    return result;
}

void drawPatch(const FacadeSurface& surface, const StitchPlan& plan,
               std::span<const WallPoint> outline, const CameraPose& pose, double focalPixels,
               const ImageView& photo, const CanvasView& canvas, int featherPixels) {
    if (outline.size() < 3 || plan.unitsPerPixel <= 0.0) {
        return;
    }
    std::vector<double> xs;
    std::vector<double> ys;
    xs.reserve(outline.size());
    ys.reserve(outline.size());
    for (const WallPoint& point : outline) {
        xs.push_back((point.arc - plan.leftArc) / plan.unitsPerPixel);
        ys.push_back((plan.topHeight - point.height) / plan.unitsPerPixel);
    }
    const int margin = featherPixels + kBrightnessRingPixels;
    const int firstX =
        std::max(0, static_cast<int>(std::floor(*std::min_element(xs.begin(), xs.end()))) - margin);
    const int endX =
        std::min(canvas.width,
                 static_cast<int>(std::ceil(*std::max_element(xs.begin(), xs.end()))) + margin + 1);
    const int firstY =
        std::max(0, static_cast<int>(std::floor(*std::min_element(ys.begin(), ys.end()))) - margin);
    const int endY =
        std::min(canvas.height,
                 static_cast<int>(std::ceil(*std::max_element(ys.begin(), ys.end()))) + margin + 1);
    if (endX <= firstX || endY <= firstY) {
        return;
    }
    const int width = endX - firstX;
    const int height = endY - firstY;
    const auto columns = static_cast<std::size_t>(width);

    // Fill the outline, using the rule that a point is inside if a line from it crosses an odd
    // number of edges.
    std::vector<std::uint8_t> mask(columns * static_cast<std::size_t>(height), 0U);
    std::vector<double> crossings;
    for (int y = 0; y < height; ++y) {
        const double rowY = firstY + y + 0.5;
        crossings.clear();
        for (std::size_t index = 0; index < xs.size(); ++index) {
            const std::size_t next = (index + 1) % xs.size();
            if ((ys[index] <= rowY) != (ys[next] <= rowY)) {
                const double share = (rowY - ys[index]) / (ys[next] - ys[index]);
                crossings.push_back(xs[index] + (xs[next] - xs[index]) * share);
            }
        }
        std::sort(crossings.begin(), crossings.end());
        std::uint8_t* row = mask.data() + static_cast<std::size_t>(y) * columns;
        for (std::size_t index = 0; index + 1 < crossings.size(); index += 2) {
            const int from =
                std::max(0, static_cast<int>(std::ceil(crossings[index] - firstX - 0.5)));
            const int to =
                std::min(width, static_cast<int>(std::ceil(crossings[index + 1] - firstX - 0.5)));
            for (int x = from; x < to; ++x) {
                row[x] = static_cast<std::uint8_t>(kFullMask);
            }
        }
    }
    std::vector<std::uint8_t> scratch;
    for (int y = 0; y < height; ++y) {
        blurLine(mask.data() + static_cast<std::size_t>(y) * columns, 1, width, featherPixels,
                 scratch);
    }
    for (int x = 0; x < width; ++x) {
        blurLine(mask.data() + x, columns, height, featherPixels, scratch);
    }

    std::vector<std::uint32_t> pixels =
        projectPhoto(surface, plan, pose, focalPixels, photo, firstX, endX, firstY, endY);
    // Inside the area the images differ on purpose, such as a tower instead of sky, so only the
    // ring around it can say how their brightness compares.
    std::vector<std::uint32_t> ring = pixels;
    for (std::size_t index = 0; index < ring.size(); ++index) {
        if (mask[index] != 0U) {
            ring[index] = 0U;
        }
    }
    const ImageView ringView{ring.data(), width, height, columns * sizeof(std::uint32_t)};
    const auto gain256 = static_cast<std::uint32_t>(
        std::lround(brightnessGain(canvas, ringView, firstX, firstY) * 256.0));
    const ImageView region{pixels.data(), width, height, columns * sizeof(std::uint32_t)};
    for (int y = 0; y < height; ++y) {
        std::uint32_t* output = rowOf(canvas, firstY + y) + firstX;
        const std::uint32_t* incoming = rowOf(region, y);
        const std::uint8_t* weights = mask.data() + static_cast<std::size_t>(y) * columns;
        for (int x = 0; x < width; ++x) {
            const std::uint32_t weight = weights[x];
            if (weight == 0U || (incoming[x] >> 24U) == 0U) {
                continue;
            }
            const std::uint32_t value = withGain(incoming[x], gain256);
            output[x] = (output[x] >> 24U) == 0U
                            ? value
                            : mixPixels(output[x], value, (weight * 256U) / kFullMask);
        }
    }
}

}  // namespace streetpano::core
