#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/camera_pose.hpp"
#include "core/vec3.hpp"

namespace streetpano::core {

struct SurfaceHit {
    double arc = 0.0;
    double height = 0.0;
};

// A vertical wall that follows the buildings along the walk. Seen from above it's a line of
// connected segments, so laying it out flat gives a straight panorama: distance along the wall
// (arc) runs across the panorama, and height runs up it.
struct FacadeSurface {
    struct Node {
        Vec3 point;  // On the plane through the origin that's perpendicular to up.
        double arc = 0.0;
    };

    Vec3 up;
    std::vector<Node> nodes;
    double medianCameraDistance = 0.0;

    [[nodiscard]] Vec3 pointAt(double arc, double height) const;
    // Returns where a ray first meets the side of the wall that faces the cameras.
    [[nodiscard]] std::optional<SurfaceHit> hit(Vec3 from, Vec3 direction) const;
};

struct SurfaceResult {
    std::optional<FacadeSurface> surface;
    std::string error;
};

// Measures the distance to the buildings at each camera, using points above camera height and
// ignoring anything much nearer or further than the facades. It then joins those measurements
// into a smooth wall. Gaps between
// buildings are bridged from either side, which keeps the scale steady across them.
[[nodiscard]] SurfaceResult fitFacadeSurface(std::span<const CameraPose> poses,
                                             std::span<const Vec3> points);

}  // namespace streetpano::core
