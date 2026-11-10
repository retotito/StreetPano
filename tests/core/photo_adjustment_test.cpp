#include "core/photo_adjustment.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "core/stitch_plan.hpp"

using Catch::Approx;
using streetpano::core::adjustedPose;
using streetpano::core::CameraPose;
using streetpano::core::FacadeSurface;
using streetpano::core::makeStitchPlan;
using streetpano::core::Mat3;
using streetpano::core::PhotoAdjustment;
using streetpano::core::photoBox;
using streetpano::core::Vec3;

namespace {

constexpr Mat3 kIdentity = {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};
constexpr double kFocal = 800.0;

FacadeSurface flatWall() {
    FacadeSurface surface;
    surface.up = {0.0, -1.0, 0.0};
    surface.medianCameraDistance = 15.0;
    surface.nodes.push_back({Vec3{-1000.0, 0.0, 15.0}, 0.0});
    surface.nodes.push_back({Vec3{1000.0, 0.0, 15.0}, 2000.0});
    return surface;
}

CameraPose camera() {
    CameraPose pose;
    pose.worldToCamera = kIdentity;
    return pose;
}

}  // namespace

TEST_CASE("no correction leaves the camera as it was") {
    const CameraPose adjusted = adjustedPose(camera(), {}, flatWall());
    REQUIRE(adjusted.center.x == 0.0);
    REQUIRE(adjusted.worldToCamera[2].z == 1.0);
    REQUIRE(PhotoAdjustment{}.isNeutral());
    REQUIRE_FALSE(PhotoAdjustment{0.0, 0.0, 0.0, 1.01}.isNeutral());
}

TEST_CASE("turning the photo turns the camera about its view") {
    const CameraPose adjusted = adjustedPose(camera(), {10.0, 0.0, 0.0, 1.0}, flatWall());
    REQUIRE(adjusted.forward().z == Approx(1.0));
    // The camera's up, measured in the world, leans by the same angle.
    const Vec3 up = adjusted.up();
    REQUIRE(std::abs(std::atan2(up.x, -up.y)) * 180.0 / 3.14159265358979 == Approx(10.0));
}

TEST_CASE("tilting the photo tips the camera's view up or down") {
    const CameraPose adjusted = adjustedPose(camera(), {0.0, 5.0, 0.0, 1.0}, flatWall());
    REQUIRE(std::abs(adjusted.forward().y) == Approx(std::sin(5.0 * 3.14159265358979 / 180.0)));
    REQUIRE(adjusted.forward().x == Approx(0.0).margin(1e-12));
}

TEST_CASE("moving and scaling change where the photo lands on the wall") {
    const auto surface = flatWall();
    const auto plan =
        *makeStitchPlan(surface, std::vector<CameraPose>{camera()}, kFocal, 640, 360, {}).plan;
    const auto box = *photoBox(surface, plan, camera(), kFocal, 640, 360);
    const auto up = *photoBox(surface, plan, adjustedPose(camera(), {0.0, 0.0, 1.0, 1.0}, surface),
                              kFocal, 640, 360);
    // A photo moved up the wall lands that many pixels higher.
    REQUIRE(up.top - box.top == Approx(-1.0 / plan.unitsPerPixel).margin(1.0));
    const auto larger = *photoBox(
        surface, plan, adjustedPose(camera(), {0.0, 0.0, 0.0, 1.25}, surface), kFocal, 640, 360);
    // Seen from nearer the wall, the photo covers less of it, so its content is shown larger.
    REQUIRE((larger.right - larger.left) == Approx((box.right - box.left) / 1.25).margin(2.0));
}

TEST_CASE("scaling a camera that doesn't see the wall only moves it up or down") {
    CameraPose away = camera();
    away.worldToCamera = {Vec3{-1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, -1}};
    const CameraPose adjusted = adjustedPose(away, {0.0, 0.0, 0.5, 1.5}, flatWall());
    REQUIRE(adjusted.center.x == 0.0);
    REQUIRE(adjusted.center.z == 0.0);
    REQUIRE(adjusted.center.y == Approx(-0.5));  // Up is -Y here.
}
