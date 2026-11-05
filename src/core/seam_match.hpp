#pragma once

#include <optional>
#include <span>
#include <vector>

#include "core/camera_pose.hpp"
#include "core/facade_surface.hpp"
#include "core/stitch_plan.hpp"

namespace streetpano::core {

struct SeamMatch {
    // How far the wall should move, away from the cameras, to make the two photos agree.
    // Empty when the photos can't say, for example on a plain wall.
    std::optional<double> offset;
    double scoreAtWall = 0.0;  // How well the photos agree now, from -1 to 1.
    double bestScore = 0.0;
};

struct SeamPhoto {
    const CameraPose* pose = nullptr;
    ImageView image;
};

// Two photos of the same wall line up only if the wall is at the right distance. This tries
// nearby distances at one place on the wall, above camera height, and keeps the best.
[[nodiscard]] SeamMatch matchAtSeam(const FacadeSurface& surface, double arc,
                                    const SeamPhoto& first, const SeamPhoto& second,
                                    double focalPixels);

// Turns offsets at seams into one offset per wall sample. Each seam moves the samples on either
// side of it, and seams without an offset move nothing.
struct SeamOffset {
    double arc = 0.0;
    double offset = 0.0;
};
[[nodiscard]] std::vector<double> sampleOffsets(const FacadeSurface& surface,
                                                std::span<const SeamOffset> seams);

}  // namespace streetpano::core
