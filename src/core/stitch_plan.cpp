#include "core/stitch_plan.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

namespace streetpano::core {
namespace {

constexpr double kMinFacingRay = 0.05;       // Rays this close to parallel never reach the facade.
constexpr double kMaxExtentDistances = 8.0;  // Limits grazing rays, in camera distances.

struct Extent {
    double left = std::numeric_limits<double>::infinity();
    double right = -std::numeric_limits<double>::infinity();
    double bottom = std::numeric_limits<double>::infinity();
    double top = -std::numeric_limits<double>::infinity();
};

[[nodiscard]] std::optional<Vec3> hitPlane(const StreetFrame& frame, Vec3 from, Vec3 direction) {
    const double facing = dot(direction, frame.normal);
    if (facing < kMinFacingRay) {
        return std::nullopt;
    }
    const double distance = dot(frame.origin - from, frame.normal) / facing;
    if (distance <= 0.0) {
        return std::nullopt;
    }
    return from + direction * distance;
}

[[nodiscard]] Extent footprint(const StreetFrame& frame, const CameraPose& pose, double focal,
                               int width, int height) {
    const double halfWidth = width / 2.0;
    const double halfHeight = height / 2.0;
    const std::array<Vec3, 4> corners = {
        Vec3{-halfWidth, -halfHeight, focal}, Vec3{halfWidth, -halfHeight, focal},
        Vec3{-halfWidth, halfHeight, focal}, Vec3{halfWidth, halfHeight, focal}};
    const double limit = frame.medianCameraDistance * kMaxExtentDistances;
    const double centerU = dot(pose.center - frame.origin, frame.right);
    const double centerV = dot(pose.center - frame.origin, frame.up);
    Extent extent;
    for (const Vec3& corner : corners) {
        const std::optional<Vec3> hit =
            hitPlane(frame, pose.center, multiplyTransposed(pose.worldToCamera, corner));
        if (hit.has_value()) {
            const double u =
                std::clamp(dot(*hit - frame.origin, frame.right), centerU - limit, centerU + limit);
            const double v =
                std::clamp(dot(*hit - frame.origin, frame.up), centerV - limit, centerV + limit);
            extent.left = std::min(extent.left, u);
            extent.right = std::max(extent.right, u);
            extent.bottom = std::min(extent.bottom, v);
            extent.top = std::max(extent.top, v);
        }
    }
    return extent;
}

[[nodiscard]] std::uint32_t mixPixels(std::uint32_t from, std::uint32_t to, std::uint32_t weight) {
    // Weight runs from 0 to 256. Masks keep each pair of channels from overflowing.
    const std::uint32_t inverse = 256U - weight;
    const std::uint32_t redBlue =
        (((from & 0x00FF00FFU) * inverse + (to & 0x00FF00FFU) * weight) >> 8U) & 0x00FF00FFU;
    const std::uint32_t green =
        (((from & 0x0000FF00U) * inverse + (to & 0x0000FF00U) * weight) >> 8U) & 0x0000FF00U;
    return 0xFF000000U | redBlue | green;
}

[[nodiscard]] const std::uint32_t* rowOf(const ImageView& image, int y) {
    const auto* start = reinterpret_cast<const unsigned char*>(image.pixels);
    return reinterpret_cast<const std::uint32_t*>(start +
                                                  static_cast<std::size_t>(y) * image.bytesPerLine);
}

[[nodiscard]] std::uint32_t* rowOf(const CanvasView& canvas, int y) {
    auto* start = reinterpret_cast<unsigned char*>(canvas.pixels);
    return reinterpret_cast<std::uint32_t*>(start +
                                            static_cast<std::size_t>(y) * canvas.bytesPerLine);
}

[[nodiscard]] std::uint32_t sampleBilinear(const ImageView& photo, double x, double y) {
    const int left = static_cast<int>(x);
    const int top = static_cast<int>(y);
    const auto weightX = static_cast<std::uint32_t>((x - left) * 256.0);
    const auto weightY = static_cast<std::uint32_t>((y - top) * 256.0);
    const std::uint32_t* upper = rowOf(photo, top) + left;
    const std::uint32_t* lower = rowOf(photo, top + 1) + left;
    return mixPixels(mixPixels(upper[0], upper[1], weightX), mixPixels(lower[0], lower[1], weightX),
                     weightY);
}

}  // namespace

PlanResult makeStitchPlan(const StreetFrame& frame, std::span<const CameraPose> poses,
                          double focalPixels, int photoWidth, int photoHeight,
                          const StitchOptions& options) {
    PlanResult result;
    if (focalPixels <= 0.0 || photoWidth <= 0 || photoHeight <= 0) {
        result.error = "The camera's focal length or photo size is invalid.";
        return result;
    }

    struct Placed {
        std::size_t camera;
        double axisU;
    };
    std::vector<Placed> placed;
    placed.reserve(poses.size());
    Extent total;
    for (std::size_t index = 0; index < poses.size(); ++index) {
        const std::optional<Vec3> axis =
            hitPlane(frame, poses[index].center, poses[index].forward());
        if (!axis.has_value()) {
            continue;
        }
        placed.push_back(Placed{index, dot(*axis - frame.origin, frame.right)});
        const Extent extent = footprint(frame, poses[index], focalPixels, photoWidth, photoHeight);
        total.bottom = std::min(total.bottom, extent.bottom);
        total.top = std::max(total.top, extent.top);
    }
    if (placed.empty()) {
        result.error = "No photo looks towards the facade.";
        return result;
    }
    std::sort(placed.begin(), placed.end(),
              [](const Placed& a, const Placed& b) { return a.axisU < b.axisU; });

    const Extent first =
        footprint(frame, poses[placed.front().camera], focalPixels, photoWidth, photoHeight);
    const Extent last =
        footprint(frame, poses[placed.back().camera], focalPixels, photoWidth, photoHeight);
    total.left = first.left;
    total.right = last.right;

    const double fullUnitsPerPixel = frame.medianCameraDistance / focalPixels;
    const double fullWidth = (total.right - total.left) / fullUnitsPerPixel;
    const double fullHeight = (total.top - total.bottom) / fullUnitsPerPixel;
    if (!(fullWidth > 0.0) || !(fullHeight > 0.0)) {
        result.error = "The photos don't cover any of the facade.";
        return result;
    }
    const double scale =
        std::min({1.0, std::sqrt(static_cast<double>(options.maxPixels) / (fullWidth * fullHeight)),
                  options.maxSide / fullWidth, options.maxSide / fullHeight});

    StitchPlan plan;
    plan.scale = scale;
    plan.unitsPerPixel = fullUnitsPerPixel / scale;
    plan.width = std::max(1, static_cast<int>(std::floor(fullWidth * scale)));
    plan.height = std::max(1, static_cast<int>(std::floor(fullHeight * scale)));
    plan.leftOnPlane = total.left;
    plan.topOnPlane = total.top;

    int xStart = 0;
    for (std::size_t index = 0; index < placed.size(); ++index) {
        int xEnd = plan.width;
        if (index + 1 < placed.size()) {
            const double boundary = (placed[index].axisU + placed[index + 1].axisU) / 2.0;
            xEnd = std::clamp(
                static_cast<int>(std::lround((boundary - total.left) / plan.unitsPerPixel)), xStart,
                plan.width);
        }
        if (xEnd > xStart) {
            plan.strips.push_back(Strip{placed[index].camera, xStart, xEnd});
        }
        xStart = xEnd;
    }
    result.plan = std::move(plan);
    return result;
}

void drawStrip(const StreetFrame& frame, const StitchPlan& plan, const Strip& strip,
               const CameraPose& pose, double focalPixels, const ImageView& photo,
               const CanvasView& canvas, int featherPixels) {
    const int firstX = std::max(0, strip.xStart - featherPixels);
    const int endX = std::min(canvas.width, strip.xEnd + featherPixels);
    const int rampStart = strip.xStart - featherPixels;
    const int rampWidth = 2 * featherPixels;

    // Camera coordinates change linearly across the panorama, which keeps this loop cheap.
    const Vec3 topLeft = frame.origin + frame.right * plan.leftOnPlane + frame.up * plan.topOnPlane;
    const Vec3 start = multiply(pose.worldToCamera, topLeft - pose.center);
    const Vec3 stepX = multiply(pose.worldToCamera, frame.right * plan.unitsPerPixel);
    const Vec3 stepY = multiply(pose.worldToCamera, frame.up * -plan.unitsPerPixel);
    const double centerX = photo.width / 2.0;
    const double centerY = photo.height / 2.0;
    const double maxSampleX = photo.width - 1.0;
    const double maxSampleY = photo.height - 1.0;

    for (int y = 0; y < canvas.height; ++y) {
        std::uint32_t* output = rowOf(canvas, y);
        const Vec3 rowStart = start + stepY * (y + 0.5);
        for (int x = firstX; x < endX; ++x) {
            const Vec3 point = rowStart + stepX * (x + 0.5);
            if (point.z <= 0.0) {
                continue;
            }
            // Pixel centres sit at half coordinates, in both the panorama and the photo.
            const double sampleX = focalPixels * point.x / point.z + centerX - 0.5;
            const double sampleY = focalPixels * point.y / point.z + centerY - 0.5;
            if (sampleX < 0.0 || sampleY < 0.0 || sampleX >= maxSampleX || sampleY >= maxSampleY) {
                continue;
            }
            const std::uint32_t value = sampleBilinear(photo, sampleX, sampleY);
            const std::uint32_t existing = output[x];
            if (rampWidth > 0 && x < rampStart + rampWidth && (existing >> 24U) != 0U) {
                const auto weight =
                    static_cast<std::uint32_t>(((x - rampStart) * 2 + 1) * 128 / rampWidth);
                output[x] = mixPixels(existing, value, weight);
            } else {
                output[x] = value;
            }
        }
    }
}

}  // namespace streetpano::core
