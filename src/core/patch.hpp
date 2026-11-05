#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "core/camera_pose.hpp"
#include "core/facade_surface.hpp"
#include "core/pixels.hpp"
#include "core/stitch_plan.hpp"

namespace streetpano::core {

struct WallPoint {
    double arc = 0.0;
    double height = 0.0;
};

// A saved decision: this area of the wall takes its pixels from this photo. Keeping an object
// whole and removing one are both this, with a different photo chosen. Positions are on the wall
// rather than in panorama pixels, so a decision survives a change of panorama size.
struct Patch {
    std::vector<WallPoint> outline;
    std::string photo;
};

// Returns the photos that can supply an area, best first: those that see all of it, then the
// nearest to looking straight at it. A photo that can't see the area's middle isn't included.
[[nodiscard]] std::vector<std::size_t> patchChoices(const FacadeSurface& surface,
                                                    std::span<const CameraPose> poses,
                                                    std::span<const WallPoint> outline,
                                                    double focalPixels, int photoWidth,
                                                    int photoHeight);

// Draws an area of a photo onto the panorama, with a soft edge, matching the brightness of the
// ring around the area. Pixels the
// photo can't see are left as they are. focalPixels must match the image's size.
void drawPatch(const FacadeSurface& surface, const StitchPlan& plan,
               std::span<const WallPoint> outline, const CameraPose& pose, double focalPixels,
               const ImageView& photo, const CanvasView& canvas, int featherPixels);

}  // namespace streetpano::core
