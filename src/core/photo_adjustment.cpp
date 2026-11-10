#include "core/photo_adjustment.hpp"

#include <cmath>
#include <numbers>

namespace streetpano::core {
namespace {

[[nodiscard]] Mat3 times(const Mat3& a, const Mat3& b) {
    Mat3 result{};
    for (std::size_t row = 0; row < 3; ++row) {
        result[row] = multiplyTransposed(b, a[row]);
    }
    return result;
}

[[nodiscard]] double radians(double degrees) { return degrees * std::numbers::pi / 180.0; }

}  // namespace

CameraPose adjustedPose(const CameraPose& pose, const PhotoAdjustment& adjustment,
                        const FacadeSurface& surface) {
    if (adjustment.isNeutral()) {
        return pose;
    }
    CameraPose result = pose;
    // Rotations about the camera's own axes: Z points along the view and X to the right, with
    // Y down, so turning the camera clockwise turns the photo anticlockwise.
    const double roll = radians(adjustment.rollDegrees);
    const double pitch = radians(adjustment.pitchDegrees);
    const Mat3 aboutView = {Vec3{std::cos(roll), std::sin(roll), 0.0},
                            Vec3{-std::sin(roll), std::cos(roll), 0.0}, Vec3{0.0, 0.0, 1.0}};
    const Mat3 aboutRight = {Vec3{1.0, 0.0, 0.0}, Vec3{0.0, std::cos(pitch), -std::sin(pitch)},
                             Vec3{0.0, std::sin(pitch), std::cos(pitch)}};
    result.worldToCamera = times(aboutRight, times(aboutView, pose.worldToCamera));

    // Raising the camera raises everything it sees on the wall, so the photo lands higher.
    result.center = pose.center + surface.up * adjustment.shift;
    if (adjustment.scale > 0.0 && adjustment.scale != 1.0) {
        const auto hit = surface.hit(pose.center, pose.forward());
        if (hit.has_value()) {
            const Vec3 onWall = surface.pointAt(hit->arc, hit->height);
            const Vec3 towardsWall = onWall - pose.center;
            result.center = result.center + towardsWall * (1.0 - 1.0 / adjustment.scale);
        }
    }
    return result;
}

}  // namespace streetpano::core
