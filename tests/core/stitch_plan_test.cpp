#include "core/stitch_plan.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdlib>
#include <vector>

using Catch::Approx;
using streetpano::core::brightnessGain;
using streetpano::core::CameraPose;
using streetpano::core::CanvasView;
using streetpano::core::drawStrip;
using streetpano::core::FacadeSurface;
using streetpano::core::findSeam;
using streetpano::core::ImageView;
using streetpano::core::makeStitchPlan;
using streetpano::core::Mat3;
using streetpano::core::StitchOptions;
using streetpano::core::Vec3;

namespace {

constexpr Mat3 kIdentity = {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};
constexpr double kFocal = 800.0;

// A flat wall 15 units in front of the cameras, with arc 1000 at x = 0.
FacadeSurface flatWall() {
    FacadeSurface surface;
    surface.up = {0.0, -1.0, 0.0};
    surface.medianCameraDistance = 15.0;
    surface.nodes.push_back({Vec3{-1000.0, 0.0, 15.0}, 0.0});
    surface.nodes.push_back({Vec3{1000.0, 0.0, 15.0}, 2000.0});
    return surface;
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
    const auto result = makeStitchPlan(flatWall(), street(10), kFocal, 1280, 720, {});
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
    const auto result = makeStitchPlan(flatWall(), street(10), kFocal, 1280, 720, {});
    REQUIRE(result.plan.has_value());
    REQUIRE(result.plan->scale == 1.0);
    REQUIRE(result.plan->unitsPerPixel == Approx(15.0 / kFocal));
    // One photo's width, plus 9 m of walking at this resolution.
    REQUIRE(std::abs(result.plan->width - (1280 + 9 * 800 / 15)) <= 1);
    REQUIRE(result.plan->height == 720);
}

TEST_CASE("each strip is centred on its photo's view") {
    const auto result = makeStitchPlan(flatWall(), street(10), kFocal, 1280, 720, {});
    REQUIRE(result.plan.has_value());
    REQUIRE(result.plan->strips[3].camera == 3);
    const int stripWidth = result.plan->strips[3].xEnd - result.plan->strips[3].xStart;
    REQUIRE(stripWidth == Approx(800.0 / 15.0).margin(1.0));
}

TEST_CASE("photos taken out of order are still placed correctly") {
    auto poses = street(5);
    std::swap(poses[1], poses[3]);
    const auto result = makeStitchPlan(flatWall(), poses, kFocal, 1280, 720, {});
    REQUIRE(result.plan.has_value());
    REQUIRE(result.plan->strips[1].camera == 3);
    REQUIRE(result.plan->strips[3].camera == 1);
}

TEST_CASE("a memory budget reduces the panorama") {
    StitchOptions options;
    options.maxPixels = 100000;
    const auto result = makeStitchPlan(flatWall(), street(10), kFocal, 1280, 720, options);
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
    const auto result = makeStitchPlan(flatWall(), street(10), kFocal, 1280, 720, options);
    REQUIRE(result.plan.has_value());
    REQUIRE(result.plan->width <= 500);
}

TEST_CASE("invalid input gives clear errors") {
    SECTION("bad focal length") {
        REQUIRE_FALSE(makeStitchPlan(flatWall(), street(2), 0.0, 1280, 720, {}).plan);
    }
    SECTION("bad photo size") {
        REQUIRE_FALSE(makeStitchPlan(flatWall(), street(2), kFocal, 0, 720, {}).plan);
    }
    SECTION("no photo looks at the facade") {
        auto poses = street(2);
        const Mat3 backwards = {Vec3{-1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, -1}};
        poses[0].worldToCamera = backwards;
        poses[1].worldToCamera = backwards;
        const auto result = makeStitchPlan(flatWall(), poses, kFocal, 1280, 720, {});
        REQUIRE_FALSE(result.plan.has_value());
        REQUIRE(result.error.find("looks towards") != std::string::npos);
    }
}

TEST_CASE("a camera looking away is left out, and the others cover its place") {
    auto poses = street(3);
    poses[1].worldToCamera = {Vec3{-1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, -1}};
    const auto result = makeStitchPlan(flatWall(), poses, kFocal, 1280, 720, {});
    REQUIRE(result.plan.has_value());
    REQUIRE(result.plan->strips.size() == 2);
}

TEST_CASE("drawing stays inside the canvas and the photo") {
    const auto surface = flatWall();
    const auto poses = street(3);
    const auto result = makeStitchPlan(surface, poses, kFocal, 64, 36, {});
    REQUIRE(result.plan.has_value());
    const auto& plan = *result.plan;

    const std::vector<std::uint32_t> photoPixels(64 * 36, 0xFF102030U);
    std::vector<std::uint32_t> canvasPixels(
        static_cast<std::size_t>(plan.width) * static_cast<std::size_t>(plan.height), 0U);
    const ImageView photo{photoPixels.data(), 64, 36, 64 * sizeof(std::uint32_t)};
    const CanvasView canvas{canvasPixels.data(), plan.width, plan.height,
                            static_cast<std::size_t>(plan.width) * sizeof(std::uint32_t)};
    for (const auto& strip : plan.strips) {
        drawStrip(surface, plan, strip, poses[strip.camera], kFocal, photo, canvas, 8);
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
    const auto surface = flatWall();
    const auto poses = street(1);
    const auto result = makeStitchPlan(surface, poses, kFocal, 64, 36, {});
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
    drawStrip(surface, plan, plan.strips[0], behind, kFocal, photo, canvas, 0);
    for (const std::uint32_t pixel : canvasPixels) {
        REQUIRE(pixel == 0U);
    }
}

TEST_CASE("a strip outside the canvas draws nothing") {
    const auto surface = flatWall();
    const auto poses = street(1);
    const auto result = makeStitchPlan(surface, poses, kFocal, 64, 36, {});
    REQUIRE(result.plan.has_value());
    const auto& plan = *result.plan;
    const std::vector<std::uint32_t> photoPixels(64 * 36, 0xFF102030U);
    std::vector<std::uint32_t> canvasPixels(
        static_cast<std::size_t>(plan.width) * static_cast<std::size_t>(plan.height), 0U);
    const ImageView photo{photoPixels.data(), 64, 36, 64 * sizeof(std::uint32_t)};
    const CanvasView canvas{canvasPixels.data(), plan.width, plan.height,
                            static_cast<std::size_t>(plan.width) * sizeof(std::uint32_t)};
    streetpano::core::Strip outside{0, plan.width + 1000, plan.width + 1100};
    drawStrip(surface, plan, outside, poses[0], kFocal, photo, canvas, 0);
    for (const std::uint32_t pixel : canvasPixels) {
        REQUIRE(pixel == 0U);
    }
}

namespace {

constexpr int kSeamWidth = 60;
constexpr int kSeamHeight = 40;

struct SeamScene {
    std::vector<std::uint32_t> existing =
        std::vector<std::uint32_t>(kSeamWidth * kSeamHeight, 0xFF808080U);
    std::vector<std::uint32_t> incoming =
        std::vector<std::uint32_t>(kSeamWidth * kSeamHeight, 0xFF808080U);

    [[nodiscard]] CanvasView canvas() {
        return {existing.data(), kSeamWidth, kSeamHeight, kSeamWidth * sizeof(std::uint32_t)};
    }
    [[nodiscard]] ImageView region() const {
        return {incoming.data(), kSeamWidth, kSeamHeight, kSeamWidth * sizeof(std::uint32_t)};
    }
    // Something that's in the new photo only, such as a car seen from another place.
    void paintIncoming(int left, int right, int top, int bottom, std::uint32_t colour) {
        for (int y = top; y < bottom; ++y) {
            for (int x = left; x < right; ++x) {
                incoming[static_cast<std::size_t>(y * kSeamWidth + x)] = colour;
            }
        }
    }
    void paintExisting(int left, int right, int top, int bottom, std::uint32_t colour) {
        for (int y = top; y < bottom; ++y) {
            for (int x = left; x < right; ++x) {
                existing[static_cast<std::size_t>(y * kSeamWidth + x)] = colour;
            }
        }
    }
};

}  // namespace

TEST_CASE("a seam stays at its preferred place when the photos agree") {
    SeamScene scene;
    const auto seam = findSeam(scene.canvas(), scene.region(), 0, 30, 30);
    REQUIRE(seam.size() == kSeamHeight);
    for (const int column : seam) {
        REQUIRE(std::abs(column - 30) <= 1);
    }
}

TEST_CASE("a seam goes around an object that the photos disagree about") {
    SeamScene scene;
    scene.paintIncoming(22, 38, 10, 30, 0xFF102030U);
    const auto seam = findSeam(scene.canvas(), scene.region(), 0, 30, 30);
    for (int y = 10; y < 30; ++y) {
        const int column = seam[static_cast<std::size_t>(y)];
        REQUIRE((column <= 22 || column >= 38));
    }
}

TEST_CASE("a seam moves by at most one column per row") {
    SeamScene scene;
    scene.paintIncoming(25, 45, 15, 25, 0xFF102030U);
    const auto seam = findSeam(scene.canvas(), scene.region(), 0, 30, 30);
    for (std::size_t y = 1; y < seam.size(); ++y) {
        REQUIRE(std::abs(seam[y] - seam[y - 1]) <= 1);
    }
}

TEST_CASE("a seam avoids where the new photo has no pixels") {
    SeamScene scene;
    scene.paintIncoming(0, 40, 0, kSeamHeight, 0U);
    const auto seam = findSeam(scene.canvas(), scene.region(), 0, 30, 30);
    for (const int column : seam) {
        REQUIRE(column >= 40);
    }
}

TEST_CASE("a seam columns are in canvas coordinates") {
    SeamScene scene;
    std::vector<std::uint32_t> wider(120 * kSeamHeight, 0xFF808080U);
    const CanvasView canvas{wider.data(), 120, kSeamHeight, 120 * sizeof(std::uint32_t)};
    const auto seam = findSeam(canvas, scene.region(), 50, 80, 30);
    for (const int column : seam) {
        REQUIRE(std::abs(column - 80) <= 1);
    }
}

TEST_CASE("a darker photo is brightened to match") {
    SeamScene scene;
    scene.paintIncoming(0, kSeamWidth, 0, kSeamHeight, 0xFF707070U);
    REQUIRE(brightnessGain(scene.canvas(), scene.region(), 0) == Approx(128.0 / 112.0));
}

TEST_CASE("an object in only one photo doesn't change its brightness") {
    SeamScene scene;
    scene.paintIncoming(10, 20, 10, 20, 0xFF000000U);
    REQUIRE(brightnessGain(scene.canvas(), scene.region(), 0) == Approx(1.0));
}

TEST_CASE("brightness matching stays gentle") {
    SeamScene scene;
    scene.paintIncoming(0, kSeamWidth, 0, kSeamHeight, 0xFF202020U);
    REQUIRE(brightnessGain(scene.canvas(), scene.region(), 0) == Approx(1.25));
    scene.paintIncoming(0, kSeamWidth, 0, kSeamHeight, 0xFFFFFFFFU);
    REQUIRE(brightnessGain(scene.canvas(), scene.region(), 0) == Approx(0.8));
}

TEST_CASE("brightness is left alone without an overlap or on a black photo") {
    SeamScene scene;
    scene.paintExisting(0, kSeamWidth, 0, kSeamHeight, 0U);
    REQUIRE(brightnessGain(scene.canvas(), scene.region(), 0) == 1.0);
    SeamScene black;
    black.paintIncoming(0, kSeamWidth, 0, kSeamHeight, 0xFF000000U);
    REQUIRE(brightnessGain(black.canvas(), black.region(), 0) == 1.0);
}

TEST_CASE("the new photo takes over from the seam, with a blend across it") {
    const auto surface = flatWall();
    const auto poses = street(2);
    const auto result = makeStitchPlan(surface, poses, kFocal, 64, 36, {});
    REQUIRE(result.plan.has_value());
    const auto& plan = *result.plan;
    REQUIRE(plan.strips.size() == 2);
    const std::vector<std::uint32_t> first(64 * 36, 0xFF808080U);
    const std::vector<std::uint32_t> second(64 * 36, 0xFF808080U);
    std::vector<std::uint32_t> pixels(
        static_cast<std::size_t>(plan.width) * static_cast<std::size_t>(plan.height), 0U);
    const CanvasView canvas{pixels.data(), plan.width, plan.height,
                            static_cast<std::size_t>(plan.width) * sizeof(std::uint32_t)};
    for (std::size_t index = 0; index < 2; ++index) {
        const ImageView photo{(index == 0 ? first : second).data(), 64, 36,
                              64 * sizeof(std::uint32_t)};
        drawStrip(surface, plan, plan.strips[index], poses[index], kFocal, photo, canvas, 4);
    }
    for (const std::uint32_t pixel : pixels) {
        REQUIRE((pixel == 0U || pixel == 0xFF808080U));
    }
}
