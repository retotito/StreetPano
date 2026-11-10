#pragma once

#include "core/camera_pose.hpp"
#include "core/facade_surface.hpp"

namespace streetpano::core {

// A correction the user made to one photo while aligning. Each control is a real camera
// movement, so a corrected photo still matches its neighbours wherever it was right.
struct PhotoAdjustment {
    double rollDegrees = 0.0;   // Turns the photo. Positive turns it anticlockwise.
    double pitchDegrees = 0.0;  // Tilts the camera. Positive leans vertical lines back.
    double shift = 0.0;         // Moves the photo up the wall, in the wall's units.
    double scale = 1.0;         // Makes the photo larger, as if taken nearer the wall.

    [[nodiscard]] bool isNeutral() const {
        return rollDegrees == 0.0 && pitchDegrees == 0.0 && shift == 0.0 && scale == 1.0;
    }
};

// Returns the camera as if the photo had been taken with this correction. The scale moves the
// camera along its view towards the wall, which needs the wall's distance from the camera.
[[nodiscard]] CameraPose adjustedPose(const CameraPose& pose, const PhotoAdjustment& adjustment,
                                      const FacadeSurface& surface);

}  // namespace streetpano::core
