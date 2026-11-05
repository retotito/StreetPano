#include "core/facade_surface.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace streetpano::core {
namespace {

constexpr std::size_t kMinPoints = 20;
constexpr std::size_t kMinLocalPoints = 5;
constexpr double kMinFacingRay = 0.05;  // Rays this close to parallel never reach the wall.
constexpr std::size_t kTangentReach = 2;
constexpr double kWindowSteps = 1.5;  // How far along the walk each camera looks for points.
constexpr double kBinShare = 0.05;    // Depth histogram bins, as a share of the typical depth.
constexpr double kMaxHistogramDepths = 8.0;
constexpr double kNearestFacade = 0.6;  // Nearer than this share of the typical depth is in front.
constexpr double kFurthestFacade =
    1.6;                           // Further than this share is behind, or seen through a gap.
constexpr double kMaxSlope = 0.5;  // The wall's change in depth per unit along the walk.
constexpr std::size_t kMedianReach = 3;
constexpr double kEndExtensionDistances = 8.0;

[[nodiscard]] double median(std::vector<double> values) {
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

// Returns the centre of the most crowded depth, or nothing if too few points agree.
[[nodiscard]] std::optional<double> densestDepth(std::span<const double> depths, double low,
                                                 double high, double binWidth,
                                                 std::size_t minPoints) {
    const auto binCount = static_cast<std::size_t>(std::ceil((high - low) / binWidth));
    std::vector<std::size_t> bins(binCount + 1, 0);
    for (const double depth : depths) {
        if (depth >= low && depth < high) {
            ++bins[static_cast<std::size_t>((depth - low) / binWidth)];
        }
    }
    std::size_t best = 0;
    std::size_t bestCount = 0;
    for (std::size_t index = 0; index < bins.size(); ++index) {
        // Counting the neighbours too stops a value on a bin edge from splitting its peak.
        const std::size_t count = bins[index] + (index > 0 ? bins[index - 1] : 0) +
                                  (index + 1 < bins.size() ? bins[index + 1] : 0);
        if (count > bestCount) {
            bestCount = count;
            best = index;
        }
    }
    if (bestCount < minPoints) {
        return std::nullopt;
    }
    const double center = low + (static_cast<double>(best) + 0.5) * binWidth;
    double sum = 0.0;
    std::size_t count = 0;
    for (const double depth : depths) {
        if (std::abs(depth - center) <= 1.5 * binWidth) {
            sum += depth;
            ++count;
        }
    }
    return sum / static_cast<double>(count);
}

// Gives cameras without a measurement a value between the nearest measurements on either side.
[[nodiscard]] std::vector<double> withGapsFilled(std::span<const std::optional<double>> measured,
                                                 double fallback) {
    const std::size_t count = measured.size();
    std::vector<double> filled;
    filled.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        if (measured[index].has_value()) {
            filled.push_back(measured[index].value_or(fallback));
            continue;
        }
        std::size_t next = index + 1;
        while (next < count && !measured[next].has_value()) {
            ++next;
        }
        const double after = next < count ? measured[next].value_or(fallback) : fallback;
        const double before = index > 0 ? filled.back() : after;
        const double target = next < count ? after : before;
        const double share = 1.0 / static_cast<double>(next - index + 1);
        filled.push_back(before + (target - before) * share);
    }
    return filled;
}

[[nodiscard]] std::vector<double> medianFiltered(std::span<const double> values) {
    std::vector<double> result;
    result.reserve(values.size());
    for (std::size_t index = 0; index < values.size(); ++index) {
        const std::size_t first = index - std::min(index, kMedianReach);
        const std::size_t last = std::min(values.size() - 1, index + kMedianReach);
        const auto window = values.subspan(first, last - first + 1);
        result.push_back(median(std::vector<double>(window.begin(), window.end())));
    }
    return result;
}

// Stops the wall from swinging sharply between neighbouring cameras, which would stretch it.
void limitSlope(std::vector<double>& depths, std::span<const Vec3> path) {
    for (std::size_t index = 1; index < depths.size(); ++index) {
        const double limit = kMaxSlope * length(path[index] - path[index - 1]);
        depths[index] =
            std::clamp(depths[index], depths[index - 1] - limit, depths[index - 1] + limit);
    }
    for (std::size_t index = depths.size() - 1; index > 0; --index) {
        const double limit = kMaxSlope * length(path[index] - path[index - 1]);
        depths[index - 1] =
            std::clamp(depths[index - 1], depths[index] - limit, depths[index] + limit);
    }
}

}  // namespace

Vec3 FacadeSurface::pointAt(double arc, double height) const {
    // Past either end, the first or last segment simply continues.
    const auto after =
        std::upper_bound(nodes.begin() + 1, nodes.end() - 1, arc,
                         [](double value, const Node& node) { return value < node.arc; });
    const Node& end = *after;
    const Node& start = *(after - 1);
    const double share = (arc - start.arc) / (end.arc - start.arc);
    return start.point + (end.point - start.point) * share + up * height;
}

std::optional<SurfaceHit> FacadeSurface::hit(Vec3 from, Vec3 direction) const {
    const Vec3 unit = normalized(direction);
    const double fromHeight = dot(from, up);
    const double rise = dot(unit, up);
    const Vec3 planFrom = from - up * fromHeight;
    const Vec3 planDirection = unit - up * rise;
    double nearest = std::numeric_limits<double>::infinity();
    std::optional<SurfaceHit> result;
    for (std::size_t index = 0; index + 1 < nodes.size(); ++index) {
        const Vec3 start = nodes[index].point;
        const Vec3 segment = nodes[index + 1].point - start;
        const double segmentLength = length(segment);
        const Vec3 normal = normalized(cross(up, segment));
        const double facing = dot(unit, normal);
        if (facing < kMinFacingRay) {
            continue;
        }
        const double distance = dot(start - planFrom, normal) / facing;
        if (distance <= 0.0 || distance >= nearest) {
            continue;
        }
        const double along = dot(planFrom + planDirection * distance - start, segment) /
                             (segmentLength * segmentLength);
        if (along < 0.0 || along > 1.0) {
            continue;
        }
        nearest = distance;
        result = SurfaceHit{nodes[index].arc + along * segmentLength, fromHeight + rise * distance};
    }
    return result;
}

SurfaceResult fitFacadeSurface(std::span<const CameraPose> poses, std::span<const Vec3> points) {
    SurfaceResult result;
    if (poses.empty()) {
        result.error = "There are no camera positions.";
        return result;
    }
    if (points.size() < kMinPoints) {
        result.error = "There are too few 3D points to find the facade.";
        return result;
    }

    Vec3 upSum;
    Vec3 forwardSum;
    for (const CameraPose& pose : poses) {
        upSum = upSum + pose.up();
        forwardSum = forwardSum + pose.forward();
    }
    const Vec3 up = normalized(upSum);
    const Vec3 facing = normalized(forwardSum - up * dot(forwardSum, up));
    if (length(up) == 0.0 || length(facing) == 0.0) {
        result.error = "The cameras' up direction points straight at the facade.";
        return result;
    }
    const Vec3 streetDirection = cross(facing, up);

    // The walk seen from above, in the order that it runs across the panorama.
    std::vector<Vec3> centers;
    centers.reserve(poses.size());
    for (const CameraPose& pose : poses) {
        centers.push_back(pose.center);
    }
    std::sort(centers.begin(), centers.end(), [&streetDirection](Vec3 a, Vec3 b) {
        return dot(a, streetDirection) < dot(b, streetDirection);
    });
    std::vector<Vec3> path;
    path.reserve(centers.size());
    std::vector<double> cameraHeights;
    cameraHeights.reserve(centers.size());
    for (const Vec3& center : centers) {
        path.push_back(center - up * dot(center, up));
        cameraHeights.push_back(dot(center, up));
    }

    const std::size_t count = path.size();
    std::vector<Vec3> tangents;
    tangents.reserve(count);
    std::vector<double> steps;
    for (std::size_t index = 0; index < count; ++index) {
        const Vec3 change = path[std::min(count - 1, index + kTangentReach)] -
                            path[index - std::min(index, kTangentReach)];
        tangents.push_back(length(change) > 0.0 ? normalized(change) : streetDirection);
        if (index > 0) {
            steps.push_back(length(path[index] - path[index - 1]));
        }
    }

    std::vector<double> allDepths;
    allDepths.reserve(points.size());
    for (const Vec3& point : points) {
        const Vec3 planPoint = point - up * dot(point, up);
        // The camera whose position along the walk is closest sees this point most directly.
        double nearest = std::numeric_limits<double>::infinity();
        double depth = -1.0;
        double cameraHeight = 0.0;
        for (std::size_t index = 0; index < count; ++index) {
            const double along = std::abs(dot(planPoint - path[index], tangents[index]));
            if (along < nearest) {
                nearest = along;
                depth = dot(planPoint - path[index], cross(up, tangents[index]));
                cameraHeight = cameraHeights[index];
            }
        }
        // Below the camera are the road and most parked cars, which can outnumber the facade.
        if (depth > 0.0 && dot(point, up) >= cameraHeight) {
            allDepths.push_back(depth);
        }
    }
    if (allDepths.size() < kMinPoints) {
        result.error = "No points were found in front of the cameras.";
        return result;
    }

    // First the typical facade distance over the whole street, then each camera's own.
    const double middleDepth = median(allDepths);
    const auto typical =
        densestDepth(allDepths, 0.0, middleDepth * kMaxHistogramDepths, middleDepth * kBinShare, 1);
    const double typicalDepth = typical.value_or(middleDepth);
    const double window = steps.empty() ? typicalDepth / 2.0 : median(steps) * kWindowSteps;

    std::vector<std::optional<double>> measured;
    measured.reserve(count);
    std::vector<double> nearby;
    for (std::size_t index = 0; index < count; ++index) {
        nearby.clear();
        const Vec3 normal = cross(up, tangents[index]);
        for (const Vec3& point : points) {
            const double height = dot(point, up);
            const Vec3 offset = point - up * height - path[index];
            if (height >= cameraHeights[index] &&
                std::abs(dot(offset, tangents[index])) <= window) {
                nearby.push_back(dot(offset, normal));
            }
        }
        measured.push_back(densestDepth(nearby, typicalDepth * kNearestFacade,
                                        typicalDepth * kFurthestFacade, typicalDepth * kBinShare,
                                        kMinLocalPoints));
    }
    if (std::none_of(measured.begin(), measured.end(),
                     [](const std::optional<double>& depth) { return depth.has_value(); })) {
        result.error = "No facade was found near the cameras.";
        return result;
    }
    std::vector<double> depths = medianFiltered(withGapsFilled(measured, typicalDepth));
    limitSlope(depths, path);

    FacadeSurface surface;
    surface.up = up;
    surface.medianCameraDistance = median(depths);
    const double extension = surface.medianCameraDistance * kEndExtensionDistances;
    std::vector<Vec3> line;
    line.reserve(count + 2);
    line.push_back(path.front() + cross(up, tangents.front()) * depths.front() -
                   tangents.front() * extension);
    for (std::size_t index = 0; index < count; ++index) {
        line.push_back(path[index] + cross(up, tangents[index]) * depths[index]);
    }
    line.push_back(path.back() + cross(up, tangents.back()) * depths.back() +
                   tangents.back() * extension);

    double arc = 0.0;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const double segment = index == 0 ? 0.0 : length(line[index] - line[index - 1]);
        // Cameras at one spot would make a segment without a direction.
        if (index > 0 && segment <= 0.0) {
            continue;
        }
        arc += segment;
        surface.nodes.push_back(FacadeSurface::Node{line[index], arc});
    }
    result.surface = std::move(surface);
    return result;
}

}  // namespace streetpano::core
