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

    // The measurement at one camera along the walk. These are plain data, so that a later step
    // or a manual correction can change a distance and rebuild the wall.
    struct Sample {
        Vec3 camera;  // On the same plane as the nodes.
        Vec3 tangent;
        double depth = 0.0;
        double arc = 0.0;
    };

    Vec3 up;
    std::vector<Node> nodes;
    std::vector<Sample> samples;
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

// Returns the wall with each sample's depth moved by its offset, and smoothed again. There must
// be one offset per sample.
[[nodiscard]] FacadeSurface withDepthOffsets(const FacadeSurface& surface,
                                             std::span<const double> offsets);

}  // namespace streetpano::core
