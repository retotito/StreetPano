#include "core/stitch_plan.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

namespace streetpano::core {
namespace {

constexpr double kMaxExtentDistances = 8.0;  // Limits grazing rays, in camera distances.
constexpr double kSeamReachStrips = 1.5;     // How far a seam can move, in strip widths.
constexpr double kMinGain = 0.8;             // Brightness matching stays gentle, so it can't
constexpr double kMaxGain = 1.25;            // turn a real change of light into a mistake.
constexpr float kNoPhotoCost = 30.0F;   // Discourages a seam where the new photo has no pixels.
constexpr float kOffCentreCost = 6.0F;  // At full reach. Nearer the middle has less parallax.

struct Extent {
    double left = std::numeric_limits<double>::infinity();
    double right = -std::numeric_limits<double>::infinity();
    double bottom = std::numeric_limits<double>::infinity();
    double top = -std::numeric_limits<double>::infinity();
};

[[nodiscard]] Extent footprint(const FacadeSurface& surface, const CameraPose& pose, double focal,
                               int width, int height, double axisArc) {
    const double halfWidth = width / 2.0;
    const double halfHeight = height / 2.0;
    const std::array<Vec3, 4> corners = {
        Vec3{-halfWidth, -halfHeight, focal}, Vec3{halfWidth, -halfHeight, focal},
        Vec3{-halfWidth, halfHeight, focal}, Vec3{halfWidth, halfHeight, focal}};
    const double limit = surface.medianCameraDistance * kMaxExtentDistances;
    const double centerHeight = dot(pose.center, surface.up);
    Extent extent;
    for (const Vec3& corner : corners) {
        const std::optional<SurfaceHit> hit =
            surface.hit(pose.center, multiplyTransposed(pose.worldToCamera, corner));
        if (hit.has_value()) {
            const double u = std::clamp(hit->arc, axisArc - limit, axisArc + limit);
            const double v = std::clamp(hit->height, centerHeight - limit, centerHeight + limit);
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

[[nodiscard]] std::size_t brightnessOf(std::uint32_t pixel) {
    return (((pixel >> 16U) & 0xFFU) + 2U * ((pixel >> 8U) & 0xFFU) + (pixel & 0xFFU)) / 4U;
}

[[nodiscard]] std::uint32_t withGain(std::uint32_t pixel, std::uint32_t gain256) {
    const auto scaled = [gain256](std::uint32_t channel) {
        return std::min<std::uint32_t>(255U, (channel * gain256 + 128U) >> 8U);
    };
    return 0xFF000000U | (scaled((pixel >> 16U) & 0xFFU) << 16U) |
           (scaled((pixel >> 8U) & 0xFFU) << 8U) | scaled(pixel & 0xFFU);
}

[[nodiscard]] int channelDifference(std::uint32_t a, std::uint32_t b) {
    const auto channel = [](std::uint32_t pixel, unsigned shift) {
        return static_cast<int>((pixel >> shift) & 0xFFU);
    };
    return (std::abs(channel(a, 16U) - channel(b, 16U)) +
            std::abs(channel(a, 8U) - channel(b, 8U)) + std::abs(channel(a, 0U) - channel(b, 0U))) /
           3;
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

PlanResult makeStitchPlan(const FacadeSurface& surface, std::span<const CameraPose> poses,
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
        const std::optional<SurfaceHit> axis =
            surface.hit(poses[index].center, poses[index].forward());
        if (!axis.has_value()) {
            continue;
        }
        placed.push_back(Placed{index, axis->arc});
        const Extent extent =
            footprint(surface, poses[index], focalPixels, photoWidth, photoHeight, axis->arc);
        total.bottom = std::min(total.bottom, extent.bottom);
        total.top = std::max(total.top, extent.top);
    }
    if (placed.empty()) {
        result.error = "No photo looks towards the facade.";
        return result;
    }
    std::sort(placed.begin(), placed.end(),
              [](const Placed& a, const Placed& b) { return a.axisU < b.axisU; });

    const Extent first = footprint(surface, poses[placed.front().camera], focalPixels, photoWidth,
                                   photoHeight, placed.front().axisU);
    const Extent last = footprint(surface, poses[placed.back().camera], focalPixels, photoWidth,
                                  photoHeight, placed.back().axisU);
    total.left = first.left;
    total.right = last.right;

    const double fullUnitsPerPixel = surface.medianCameraDistance / focalPixels;
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
    plan.leftArc = total.left;
    plan.topHeight = total.top;

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

double brightnessGain(const CanvasView& canvas, const ImageView& region, int firstX) {
    // Medians, because a car in only one of the photos mustn't change the whole photo.
    std::array<std::size_t, 256> existingCounts{};
    std::array<std::size_t, 256> incomingCounts{};
    std::size_t total = 0;
    for (int y = 0; y < region.height; ++y) {
        const std::uint32_t* existing = rowOf(canvas, y) + firstX;
        const std::uint32_t* incoming = rowOf(region, y);
        for (int x = 0; x < region.width; ++x) {
            if ((existing[x] >> 24U) != 0U && (incoming[x] >> 24U) != 0U) {
                ++existingCounts[brightnessOf(existing[x])];
                ++incomingCounts[brightnessOf(incoming[x])];
                ++total;
            }
        }
    }
    if (total == 0) {
        return 1.0;
    }
    const auto medianOf = [total](const std::array<std::size_t, 256>& counts) {
        std::size_t seen = 0;
        for (std::size_t level = 0; level < counts.size(); ++level) {
            seen += counts[level];
            if (seen * 2 >= total) {
                return level;
            }
        }
        return counts.size() - 1;
    };
    const std::size_t incomingMedian = medianOf(incomingCounts);
    if (incomingMedian == 0) {
        return 1.0;
    }
    return std::clamp(
        static_cast<double>(medianOf(existingCounts)) / static_cast<double>(incomingMedian),
        kMinGain, kMaxGain);
}

std::vector<int> findSeam(const CanvasView& canvas, const ImageView& region, int firstX,
                          int preferredX, int reach) {
    const int width = region.width;
    const int height = canvas.height;
    const auto columns = static_cast<std::size_t>(width);
    std::vector<float> costs(columns, 0.0F);
    std::vector<float> nextCosts(columns, 0.0F);
    std::vector<std::int8_t> steps(columns * static_cast<std::size_t>(height), 0);

    for (int y = 0; y < height; ++y) {
        const std::uint32_t* existing = rowOf(canvas, y) + firstX;
        const std::uint32_t* incoming = rowOf(region, y);
        std::int8_t* rowSteps = steps.data() + static_cast<std::size_t>(y) * columns;
        for (int x = 0; x < width; ++x) {
            const std::uint32_t a = existing[x];
            const std::uint32_t b = incoming[x];
            float cost = 0.0F;
            if ((b >> 24U) == 0U) {
                cost = (a >> 24U) == 0U ? 0.0F : kNoPhotoCost;
            } else if ((a >> 24U) != 0U) {
                cost = static_cast<float>(channelDifference(a, b));
            }
            cost += kOffCentreCost * static_cast<float>(std::abs(firstX + x - preferredX)) /
                    static_cast<float>(std::max(reach, 1));
            float best = costs[static_cast<std::size_t>(x)];
            std::int8_t step = 0;
            if (x > 0 && costs[static_cast<std::size_t>(x - 1)] < best) {
                best = costs[static_cast<std::size_t>(x - 1)];
                step = -1;
            }
            if (x + 1 < width && costs[static_cast<std::size_t>(x + 1)] < best) {
                best = costs[static_cast<std::size_t>(x + 1)];
                step = 1;
            }
            nextCosts[static_cast<std::size_t>(x)] = best + cost;
            rowSteps[x] = step;
        }
        std::swap(costs, nextCosts);
    }

    int column = static_cast<int>(std::min_element(costs.begin(), costs.end()) - costs.begin());
    std::vector<int> seam(static_cast<std::size_t>(height));
    for (int y = height - 1; y >= 0; --y) {
        seam[static_cast<std::size_t>(y)] = firstX + column;
        column += steps[static_cast<std::size_t>(y) * columns + static_cast<std::size_t>(column)];
    }
    return seam;
}

void drawStrip(const FacadeSurface& surface, const StitchPlan& plan, const Strip& strip,
               const CameraPose& pose, double focalPixels, const ImageView& photo,
               const CanvasView& canvas, int featherPixels) {
    const int stripWidth = strip.xEnd - strip.xStart;
    const int reach =
        std::max(featherPixels + 2, static_cast<int>(std::ceil(stripWidth * kSeamReachStrips)));
    const int firstX = std::max(0, strip.xStart - reach);
    const int endX = std::min(canvas.width, strip.xEnd + reach);
    if (endX <= firstX) {
        return;
    }
    const int regionWidth = endX - firstX;

    // Each column is a straight vertical line on the wall, so camera coordinates change
    // linearly down a column, which keeps the loop below cheap.
    std::vector<Vec3> columns;
    columns.reserve(static_cast<std::size_t>(regionWidth));
    for (int x = firstX; x < endX; ++x) {
        const double arc = plan.leftArc + (x + 0.5) * plan.unitsPerPixel;
        columns.push_back(
            multiply(pose.worldToCamera, surface.pointAt(arc, plan.topHeight) - pose.center));
    }
    const Vec3 stepY = multiply(pose.worldToCamera, surface.up * -plan.unitsPerPixel);
    const double centerX = photo.width / 2.0;
    const double centerY = photo.height / 2.0;
    const double maxSampleX = photo.width - 1.0;
    const double maxSampleY = photo.height - 1.0;

    const auto regionColumns = static_cast<std::size_t>(regionWidth);
    std::vector<std::uint32_t> pixels(regionColumns * static_cast<std::size_t>(canvas.height), 0U);
    for (int y = 0; y < canvas.height; ++y) {
        std::uint32_t* output = pixels.data() + static_cast<std::size_t>(y) * regionColumns;
        const Vec3 rowOffset = stepY * (y + 0.5);
        for (int x = 0; x < regionWidth; ++x) {
            const Vec3 point = columns[static_cast<std::size_t>(x)] + rowOffset;
            if (point.z <= 0.0) {
                continue;
            }
            // Pixel centres sit at half coordinates, in both the panorama and the photo.
            const double sampleX = focalPixels * point.x / point.z + centerX - 0.5;
            const double sampleY = focalPixels * point.y / point.z + centerY - 0.5;
            if (sampleX >= 0.0 && sampleY >= 0.0 && sampleX < maxSampleX && sampleY < maxSampleY) {
                output[x] = sampleBilinear(photo, sampleX, sampleY);
            }
        }
    }

    const ImageView region{pixels.data(), regionWidth, canvas.height,
                           regionColumns * sizeof(std::uint32_t)};
    const auto gain256 =
        static_cast<std::uint32_t>(std::lround(brightnessGain(canvas, region, firstX) * 256.0));
    if (gain256 != 256U) {
        for (std::uint32_t& pixel : pixels) {
            if ((pixel >> 24U) != 0U) {
                pixel = withGain(pixel, gain256);
            }
        }
    }
    const std::vector<int> seam =
        strip.xStart > 0 ? findSeam(canvas, region, firstX, strip.xStart, reach)
                         : std::vector<int>(static_cast<std::size_t>(canvas.height), 0);
    const int rampWidth = 2 * featherPixels;
    for (int y = 0; y < canvas.height; ++y) {
        std::uint32_t* output = rowOf(canvas, y);
        const std::uint32_t* incoming = rowOf(region, y);
        const int rampStart = seam[static_cast<std::size_t>(y)] - featherPixels;
        for (int x = std::max(firstX, rampStart); x < endX; ++x) {
            const std::uint32_t value = incoming[x - firstX];
            if ((value >> 24U) == 0U) {
                continue;
            }
            const std::uint32_t existing = output[x];
            if (x < rampStart + rampWidth && (existing >> 24U) != 0U) {
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
