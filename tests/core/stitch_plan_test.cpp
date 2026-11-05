#include "core/stitch_plan.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

using Catch::Approx;
using streetpano::core::CameraPose;
using streetpano::core::CanvasView;
using streetpano::core::drawStrip;
using streetpano::core::ImageView;
using streetpano::core::makeStitchPlan;
using streetpano::core::Mat3;
using streetpano::core::StitchOptions;
using streetpano::core::StreetFrame;
using streetpano::core::Vec3;

namespace {

constexpr Mat3 kIdentity = {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};
constexpr double kFocal = 800.0;

StreetFrame facadeFrame() {
    StreetFrame frame;
    frame.origin = {0.0, 0.0, 15.0};
    frame.right = {1.0, 0.0, 0.0};
    frame.up = {0.0, -1.0, 0.0};
    frame.normal = {0.0, 0.0, 1.0};
    frame.medianCameraDistance = 15.0;
    return frame;
}

std::vector<CameraPose> street(int count) {
    std::vector<CameraPose> poses;
    for (int index = 0; index < count; ++index) {
        CameraPose pose;
        pose.center = {static_cast<double>(index), 0.0, 0.0};
        pose.worldToCamera = kIdentity;
        poses.push_back(pose);
    }
    return poses;
}

}  // namespace

TEST_CASE("strips cover the whole panorama without gaps") {
    const auto result = makeStitchPlan(facadeFrame(), street(10), kFocal, 1280, 720, {});
    REQUIRE(result.plan.has_value());
    const auto& plan = *result.plan;
    REQUIRE(plan.strips.size() == 10);
    REQUIRE(plan.strips.front().xStart == 0);
    REQUIRE(plan.strips.back().xEnd == plan.width);
    for (std::size_t index = 1; index < plan.strips.size(); ++index) {
        REQUIRE(plan.strips[index].xStart == plan.strips[index - 1].xEnd);
    }
}

TEST_CASE("the panorama keeps the photos' detail") {
    const auto result = makeStitchPlan(facadeFrame(), street(10), kFocal, 1280, 720, {});
    REQUIRE(result.plan.has_value());
    REQUIRE(result.plan->scale == 1.0);
    REQUIRE(result.plan->unitsPerPixel == Approx(15.0 / kFocal));
    // One photo's width, plus 9 m of walking at this resolution.
    REQUIRE(result.plan->width == 1280 + 9 * 800 / 15);
    REQUIRE(result.plan->height == 720);
}

TEST_CASE("each strip is centred on its photo's view") {
    const auto result = makeStitchPlan(facadeFrame(), street(10), kFocal, 1280, 720, {});
    REQUIRE(result.plan.has_value());
    REQUIRE(result.plan->strips[3].camera == 3);
    const int stripWidth = result.plan->strips[3].xEnd - result.plan->strips[3].xStart;
    REQUIRE(stripWidth == Approx(800.0 / 15.0).margin(1.0));
}

TEST_CASE("photos taken out of order are still placed correctly") {
    auto poses = street(5);
    std::swap(poses[1], poses[3]);
    const auto result = makeStitchPlan(facadeFrame(), poses, kFocal, 1280, 720, {});
    REQUIRE(result.plan.has_value());
    REQUIRE(result.plan->strips[1].camera == 3);
    REQUIRE(result.plan->strips[3].camera == 1);
}

TEST_CASE("a memory budget reduces the panorama") {
    StitchOptions options;
    options.maxPixels = 100000;
    const auto result = makeStitchPlan(facadeFrame(), street(10), kFocal, 1280, 720, options);
    REQUIRE(result.plan.has_value());
    const auto& plan = *result.plan;
    REQUIRE(plan.scale < 1.0);
    REQUIRE(static_cast<std::size_t>(plan.width) * static_cast<std::size_t>(plan.height) <=
            options.maxPixels);
    REQUIRE(plan.strips.back().xEnd == plan.width);
}

TEST_CASE("the longest side is limited") {
    StitchOptions options;
    options.maxSide = 500;
    const auto result = makeStitchPlan(facadeFrame(), street(10), kFocal, 1280, 720, options);
    REQUIRE(result.plan.has_value());
    REQUIRE(result.plan->width <= 500);
}

TEST_CASE("invalid input gives clear errors") {
    SECTION("bad focal length") {
        REQUIRE_FALSE(makeStitchPlan(facadeFrame(), street(2), 0.0, 1280, 720, {}).plan);
    }
    SECTION("bad photo size") {
        REQUIRE_FALSE(makeStitchPlan(facadeFrame(), street(2), kFocal, 0, 720, {}).plan);
    }
    SECTION("no photo looks at the facade") {
        auto poses = street(2);
        const Mat3 backwards = {Vec3{-1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, -1}};
        poses[0].worldToCamera = backwards;
        poses[1].worldToCamera = backwards;
        const auto result = makeStitchPlan(facadeFrame(), poses, kFocal, 1280, 720, {});
        REQUIRE_FALSE(result.plan.has_value());
        REQUIRE(result.error.find("looks towards") != std::string::npos);
    }
}

TEST_CASE("a camera looking away is left out, and the others cover its place") {
    auto poses = street(3);
    poses[1].worldToCamera = {Vec3{-1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, -1}};
    const auto result = makeStitchPlan(facadeFrame(), poses, kFocal, 1280, 720, {});
    REQUIRE(result.plan.has_value());
    REQUIRE(result.plan->strips.size() == 2);
}

TEST_CASE("drawing stays inside the canvas and the photo") {
    const auto frame = facadeFrame();
    const auto poses = street(3);
    const auto result = makeStitchPlan(frame, poses, kFocal, 64, 36, {});
    REQUIRE(result.plan.has_value());
    const auto& plan = *result.plan;

    const std::vector<std::uint32_t> photoPixels(64 * 36, 0xFF102030U);
    std::vector<std::uint32_t> canvasPixels(
        static_cast<std::size_t>(plan.width) * static_cast<std::size_t>(plan.height), 0U);
    const ImageView photo{photoPixels.data(), 64, 36, 64 * sizeof(std::uint32_t)};
    const CanvasView canvas{canvasPixels.data(), plan.width, plan.height,
                            static_cast<std::size_t>(plan.width) * sizeof(std::uint32_t)};
    for (const auto& strip : plan.strips) {
        drawStrip(frame, plan, strip, poses[strip.camera], kFocal, photo, canvas, 8);
    }
    std::size_t drawn = 0;
    for (const std::uint32_t pixel : canvasPixels) {
        if (pixel != 0U) {
            REQUIRE(pixel == 0xFF102030U);
            ++drawn;
        }
    }
    REQUIRE(drawn > canvasPixels.size() / 2);
}

TEST_CASE("a point behind the camera isn't drawn") {
    auto frame = facadeFrame();
    const auto poses = street(1);
    const auto result = makeStitchPlan(frame, poses, kFocal, 64, 36, {});
    REQUIRE(result.plan.has_value());
    const auto& plan = *result.plan;
    CameraPose behind = poses[0];
    behind.center = {0.0, 0.0, 100.0};

    const std::vector<std::uint32_t> photoPixels(64 * 36, 0xFF102030U);
    std::vector<std::uint32_t> canvasPixels(
        static_cast<std::size_t>(plan.width) * static_cast<std::size_t>(plan.height), 0U);
    const ImageView photo{photoPixels.data(), 64, 36, 64 * sizeof(std::uint32_t)};
    const CanvasView canvas{canvasPixels.data(), plan.width, plan.height,
                            static_cast<std::size_t>(plan.width) * sizeof(std::uint32_t)};
    drawStrip(frame, plan, plan.strips[0], behind, kFocal, photo, canvas, 0);
    for (const std::uint32_t pixel : canvasPixels) {
        REQUIRE(pixel == 0U);
    }
}
