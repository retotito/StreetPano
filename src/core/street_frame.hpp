#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/vec3.hpp"

namespace streetpano::core {

// One photo's position, in COLMAP's convention: the camera looks along +Z with X to the
// right and Y down. Reconstructions have an arbitrary scale, so nothing here assumes metres.
struct CameraPose {
    std::string name;
    Vec3 center;
    Mat3 worldToCamera{};
    // Which way is up in the stored pixels. It differs for a phone held another way round.
    Vec3 upInCamera{0.0, -1.0, 0.0};

    [[nodiscard]] Vec3 forward() const {
        return multiplyTransposed(worldToCamera, {0.0, 0.0, 1.0});
    }
    [[nodiscard]] Vec3 up() const { return multiplyTransposed(worldToCamera, upInCamera); }
};

// The facade plane and its axes. Panorama pixels map to points on this plane:
// right runs along the panorama, up runs up it, and normal points away from the cameras.
struct StreetFrame {
    Vec3 origin;
    Vec3 right;
    Vec3 up;
    Vec3 normal;
    double medianCameraDistance = 0.0;
    std::size_t inlierCount = 0;
};

struct FrameResult {
    std::optional<StreetFrame> frame;
    std::string error;
};

// Finds the facade as the largest plane of points facing the cameras. This treats the whole
// street as one plane, so curved streets and very different building setbacks will misalign.
[[nodiscard]] FrameResult fitStreetFrame(std::span<const CameraPose> poses,
                                         std::span<const Vec3> points);

// Returns the eigenvector of the smallest eigenvalue of a symmetric matrix.
[[nodiscard]] Vec3 smallestEigenvector(const Mat3& matrix);

}  // namespace streetpano::core
