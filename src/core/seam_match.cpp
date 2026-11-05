#include "core/seam_match.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace streetpano::core {
namespace {

constexpr int kColumns = 48;
constexpr int kRows = 96;
constexpr int kOffsetSteps = 15;            // Tried on each side of the current wall.
constexpr double kMaxOffsetShare = 0.15;    // The furthest tried, as a share of the distance.
constexpr double kPatchWidthShare = 0.12;   // The patch's width, as a share of the distance.
constexpr double kPatchHeightShare = 0.45;  // Its height above the camera, likewise.
constexpr double kMinValidShare = 0.5;
constexpr double kMinContrast = 3.0;  // Below this standard deviation, there's nothing to match.
constexpr double kMinScore = 0.7;     // Branches give weak matches, which aren't trusted.
constexpr double kMinImprovement = 0.01;  // Nor one that barely beats the current wall.

[[nodiscard]] std::optional<double> brightnessAt(const SeamPhoto& photo, double focal, Vec3 point) {
    const Vec3 inCamera = multiply(photo.pose->worldToCamera, point - photo.pose->center);
    if (inCamera.z <= 0.0) {
        return std::nullopt;
    }
    const double x = focal * inCamera.x / inCamera.z + photo.image.width / 2.0 - 0.5;
    const double y = focal * inCamera.y / inCamera.z + photo.image.height / 2.0 - 0.5;
    if (x < 0.0 || y < 0.0 || x >= photo.image.width - 1.0 || y >= photo.image.height - 1.0) {
        return std::nullopt;
    }
    const int left = static_cast<int>(x);
    const int top = static_cast<int>(y);
    const double shareX = x - left;
    const double shareY = y - top;
    const auto brightness = [&photo](int column, int row) {
        const auto* start = reinterpret_cast<const unsigned char*>(photo.image.pixels);
        const auto* line = reinterpret_cast<const std::uint32_t*>(
            start + static_cast<std::size_t>(row) * photo.image.bytesPerLine);
        const std::uint32_t pixel = line[column];
        return static_cast<double>(((pixel >> 16U) & 0xFFU) + 2U * ((pixel >> 8U) & 0xFFU) +
                                   (pixel & 0xFFU)) /
               4.0;
    };
    const double upper =
        brightness(left, top) * (1.0 - shareX) + brightness(left + 1, top) * shareX;
    const double lower =
        brightness(left, top + 1) * (1.0 - shareX) + brightness(left + 1, top + 1) * shareX;
    return upper * (1.0 - shareY) + lower * shareY;
}

// Returns the normalised correlation of the two photos with the wall moved, if enough is seen.
[[nodiscard]] std::optional<double> scoreAt(const FacadeSurface& surface, double arc, Vec3 normal,
                                            double offset, double firstHeight,
                                            const SeamPhoto& first, const SeamPhoto& second,
                                            double focal) {
    const double width = surface.medianCameraDistance * kPatchWidthShare;
    const double height = surface.medianCameraDistance * kPatchHeightShare;
    double sumA = 0.0;
    double sumB = 0.0;
    double sumAA = 0.0;
    double sumBB = 0.0;
    double sumAB = 0.0;
    int count = 0;
    for (int row = 0; row < kRows; ++row) {
        const double up = firstHeight + height * (row + 0.5) / kRows;
        for (int column = 0; column < kColumns; ++column) {
            const double along = arc + width * ((column + 0.5) / kColumns - 0.5);
            const Vec3 point = surface.pointAt(along, up) + normal * offset;
            const auto a = brightnessAt(first, focal, point);
            const auto b = brightnessAt(second, focal, point);
            if (a.has_value() && b.has_value()) {
                sumA += *a;
                sumB += *b;
                sumAA += *a * *a;
                sumBB += *b * *b;
                sumAB += *a * *b;
                ++count;
            }
        }
    }
    if (count < static_cast<int>(kMinValidShare * kRows * kColumns)) {
        return std::nullopt;
    }
    const double total = count;
    const double varianceA = sumAA / total - (sumA / total) * (sumA / total);
    const double varianceB = sumBB / total - (sumB / total) * (sumB / total);
    if (varianceA < kMinContrast * kMinContrast || varianceB < kMinContrast * kMinContrast) {
        return std::nullopt;
    }
    return (sumAB / total - (sumA / total) * (sumB / total)) / std::sqrt(varianceA * varianceB);
}

}  // namespace

SeamMatch matchAtSeam(const FacadeSurface& surface, double arc, const SeamPhoto& first,
                      const SeamPhoto& second, double focalPixels) {
    SeamMatch match;
    const double reach = surface.medianCameraDistance * 0.01;
    const Vec3 along =
        normalized(surface.pointAt(arc + reach, 0.0) - surface.pointAt(arc - reach, 0.0));
    const Vec3 normal = cross(surface.up, along);
    const double cameraHeight =
        (dot(first.pose->center, surface.up) + dot(second.pose->center, surface.up)) / 2.0;

    const auto current =
        scoreAt(surface, arc, normal, 0.0, cameraHeight, first, second, focalPixels);
    if (!current.has_value()) {
        return match;
    }
    match.scoreAtWall = *current;
    match.bestScore = *current;
    double bestOffset = 0.0;
    int bestStep = 0;
    const double stepSize = surface.medianCameraDistance * kMaxOffsetShare / kOffsetSteps;
    for (int step = -kOffsetSteps; step <= kOffsetSteps; ++step) {
        const double offset = step * stepSize;
        const auto score =
            scoreAt(surface, arc, normal, offset, cameraHeight, first, second, focalPixels);
        if (score.has_value() && *score > match.bestScore) {
            match.bestScore = *score;
            bestOffset = offset;
            bestStep = step;
        }
    }
    // A best match at the edge of the search is noise, or a wall too far out to correct.
    if (std::abs(bestStep) < kOffsetSteps && match.bestScore >= kMinScore &&
        match.bestScore >= match.scoreAtWall + kMinImprovement) {
        match.offset = bestOffset;
    }
    return match;
}

std::vector<double> sampleOffsets(const FacadeSurface& surface, std::span<const SeamOffset> seams) {
    const std::size_t count = surface.samples.size();
    std::vector<double> sums(count, 0.0);
    std::vector<int> votes(count, 0);
    for (const SeamOffset& seam : seams) {
        const auto after = std::upper_bound(
            surface.samples.begin(), surface.samples.end(), seam.arc,
            [](double value, const FacadeSurface::Sample& sample) { return value < sample.arc; });
        const auto next = static_cast<std::size_t>(after - surface.samples.begin());
        for (const std::size_t index : {next == 0 ? std::size_t{0} : next - 1, next}) {
            if (index < count) {
                sums[index] += seam.offset;
                ++votes[index];
            }
        }
    }
    std::vector<double> offsets(count, 0.0);
    for (std::size_t index = 0; index < count; ++index) {
        if (votes[index] > 0) {
            offsets[index] = sums[index] / votes[index];
        }
    }
    return offsets;
}

}  // namespace streetpano::core
