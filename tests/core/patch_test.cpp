#include "core/patch.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <vector>

using streetpano::core::CameraPose;
using streetpano::core::CanvasView;
using streetpano::core::cleanestRegion;
using streetpano::core::drawPatch;
using streetpano::core::FacadeSurface;
using streetpano::core::ImageView;
using streetpano::core::makeStitchPlan;
using streetpano::core::Mat3;
using streetpano::core::patchChoices;
using streetpano::core::StitchPlan;
using streetpano::core::Vec3;
using streetpano::core::WallPoint;

namespace {

constexpr Mat3 kIdentity = {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};
constexpr double kFocal = 800.0;
constexpr int kPhotoWidth = 640;
constexpr int kPhotoHeight = 360;

FacadeSurface flatWall() {
    FacadeSurface surface;
    surface.up = {0.0, -1.0, 0.0};
    surface.medianCameraDistance = 15.0;
    surface.nodes.push_back({Vec3{-1000.0, 0.0, 15.0}, 0.0});
    surface.nodes.push_back({Vec3{1000.0, 0.0, 15.0}, 2000.0});
    return surface;
}

std::vector<CameraPose> street(int count, double spacing = 1.0) {
    std::vector<CameraPose> poses;
    for (int index = 0; index < count; ++index) {
        CameraPose pose;
        pose.center = {index * spacing, 0.0, 0.0};
        pose.worldToCamera = kIdentity;
        poses.push_back(pose);
    }
    return poses;
}

// A square on the wall centred in front of this position along the street.
std::vector<WallPoint> squareAt(double x, double half = 0.5) {
    const double arc = 1000.0 + x;
    return {{arc - half, half}, {arc + half, half}, {arc + half, -half}, {arc - half, -half}};
}

struct Scene {
    StitchPlan plan;
    std::vector<std::uint32_t> pixels;
    std::vector<std::uint32_t> photoPixels =
        std::vector<std::uint32_t>(kPhotoWidth * kPhotoHeight, 0xFF4080C0U);

    explicit Scene(std::uint32_t background) {
        const auto result =
            makeStitchPlan(flatWall(), street(5), kFocal, kPhotoWidth, kPhotoHeight, {});
        plan = *result.plan;
        pixels.assign(static_cast<std::size_t>(plan.width) * static_cast<std::size_t>(plan.height),
                      background);
    }
    [[nodiscard]] CanvasView canvas() {
        return {pixels.data(), plan.width, plan.height,
                static_cast<std::size_t>(plan.width) * sizeof(std::uint32_t)};
    }
    [[nodiscard]] ImageView photo() const {
        return {photoPixels.data(), kPhotoWidth, kPhotoHeight, kPhotoWidth * sizeof(std::uint32_t)};
    }
    [[nodiscard]] std::uint32_t at(double wallX, double height) const {
        const int x = static_cast<int>((1000.0 + wallX - plan.leftArc) / plan.unitsPerPixel);
        const int y = static_cast<int>((plan.topHeight - height) / plan.unitsPerPixel);
        return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(plan.width) +
                      static_cast<std::size_t>(x)];
    }
};

}  // namespace

TEST_CASE("the photo looking straight at an area is the first choice") {
    const auto choices =
        patchChoices(flatWall(), street(9), squareAt(4.0), kFocal, kPhotoWidth, kPhotoHeight);
    REQUIRE_FALSE(choices.empty());
    REQUIRE(choices.front() == 4);
    REQUIRE(choices.size() > 3);
}

TEST_CASE("photos that see all of an area come before those that see part of it") {
    // Half of this wide area is outside the field of view of the far cameras.
    const auto choices = patchChoices(flatWall(), street(30), squareAt(15.0, 3.5), kFocal,
                                      kPhotoWidth, kPhotoHeight);
    REQUIRE_FALSE(choices.empty());
    REQUIRE(std::abs(static_cast<int>(choices.front()) - 15) <= 2);
}

TEST_CASE("a photo that can't see an area isn't a choice") {
    REQUIRE(patchChoices(flatWall(), street(3), squareAt(100.0), kFocal, kPhotoWidth, kPhotoHeight)
                .empty());
    REQUIRE(patchChoices(flatWall(), street(3), {}, kFocal, kPhotoWidth, kPhotoHeight).empty());
    auto behind = street(1);
    behind[0].center.z = 100.0;
    REQUIRE(
        patchChoices(flatWall(), behind, squareAt(0.0), kFocal, kPhotoWidth, kPhotoHeight).empty());
}

TEST_CASE("a patch replaces what's inside its outline, and nothing else") {
    Scene scene(0xFF4080C0U);
    scene.photoPixels.assign(scene.photoPixels.size(), 0xFF4080C1U);
    const auto poses = street(5);
    drawPatch(flatWall(), scene.plan, squareAt(2.0), poses[2], kFocal, scene.photo(),
              scene.canvas(), 0);
    REQUIRE(scene.at(2.0, 0.0) == 0xFF4080C1U);
    REQUIRE(scene.at(2.0, 2.0) == 0xFF4080C0U);
    REQUIRE(scene.at(0.5, 0.0) == 0xFF4080C0U);
}

TEST_CASE("a patch fades in across its edge") {
    Scene scene(0xFF000000U);
    scene.photoPixels.assign(scene.photoPixels.size(), 0xFFFFFFFFU);
    const auto poses = street(5);
    drawPatch(flatWall(), scene.plan, squareAt(2.0), poses[2], kFocal, scene.photo(),
              scene.canvas(), 8);
    const auto edge = scene.at(2.5, 0.0) & 0xFFU;
    REQUIRE(edge > 40U);
    REQUIRE(edge < 215U);
    REQUIRE((scene.at(2.0, 0.0) & 0xFFU) > 200U);
}

TEST_CASE("a patch matches the brightness around it") {
    Scene scene(0xFF808080U);
    scene.photoPixels.assign(scene.photoPixels.size(), 0xFF707070U);
    const auto poses = street(5);
    drawPatch(flatWall(), scene.plan, squareAt(2.0), poses[2], kFocal, scene.photo(),
              scene.canvas(), 0);
    REQUIRE(std::abs(static_cast<int>(scene.at(2.0, 0.0) & 0xFFU) - 0x80) <= 1);
}

TEST_CASE("a change inside the area doesn't change its brightness") {
    // Sky in the panorama, and a much darker tower in the photo, with the same sky around it.
    Scene scene(0xFFC0C0C0U);
    scene.photoPixels.assign(scene.photoPixels.size(), 0xFFC0C0C0U);
    constexpr int kTower = 60;
    for (int y = kPhotoHeight / 2 - kTower; y < kPhotoHeight / 2 + kTower; ++y) {
        for (int x = kPhotoWidth / 2 - kTower; x < kPhotoWidth / 2 + kTower; ++x) {
            scene.photoPixels[static_cast<std::size_t>(y * kPhotoWidth + x)] = 0xFF505050U;
        }
    }
    const auto poses = street(5);
    drawPatch(flatWall(), scene.plan, squareAt(2.0, 1.0), poses[2], kFocal, scene.photo(),
              scene.canvas(), 0);
    REQUIRE(std::abs(static_cast<int>(scene.at(2.0, 0.0) & 0xFFU) - 0x50) <= 1);
}

TEST_CASE("a patch fills empty parts of the panorama") {
    Scene scene(0U);
    scene.photoPixels.assign(scene.photoPixels.size(), 0xFF204060U);
    const auto poses = street(5);
    drawPatch(flatWall(), scene.plan, squareAt(2.0), poses[2], kFocal, scene.photo(),
              scene.canvas(), 4);
    REQUIRE(scene.at(2.0, 0.0) == 0xFF204060U);
}

TEST_CASE("pixels the photo can't see are left alone") {
    Scene scene(0xFF808080U);
    scene.photoPixels.assign(scene.photoPixels.size(), 0xFF828282U);
    const auto poses = street(5);
    // Most of this area is too far along the street for the first photo to see.
    drawPatch(flatWall(), scene.plan, squareAt(4.4, 0.4), poses[0], kFocal, scene.photo(),
              scene.canvas(), 0);
    REQUIRE(scene.at(4.5, 0.0) == 0xFF808080U);
}

TEST_CASE("a patch that can't be drawn changes nothing") {
    Scene scene(0xFF808080U);
    const auto before = scene.pixels;
    const auto poses = street(5);
    SECTION("too few points") {
        const std::vector<WallPoint> line = {{1000.0, 0.0}, {1001.0, 0.0}};
        drawPatch(flatWall(), scene.plan, line, poses[0], kFocal, scene.photo(), scene.canvas(), 4);
    }
    SECTION("outside the panorama") {
        drawPatch(flatWall(), scene.plan, squareAt(500.0), poses[0], kFocal, scene.photo(),
                  scene.canvas(), 4);
    }
    SECTION("a plan without a size") {
        StitchPlan empty = scene.plan;
        empty.unitsPerPixel = 0.0;
        drawPatch(flatWall(), empty, squareAt(2.0), poses[0], kFocal, scene.photo(), scene.canvas(),
                  4);
    }
    REQUIRE(scene.pixels == before);
}

namespace {

constexpr int kRegion = 20;

std::vector<std::uint32_t> solidRegion(std::uint32_t colour) {
    return std::vector<std::uint32_t>(kRegion * kRegion, colour);
}

ImageView viewOf(const std::vector<std::uint32_t>& pixels) {
    return {pixels.data(), kRegion, kRegion, kRegion * sizeof(std::uint32_t)};
}

}  // namespace

TEST_CASE("the photos that agree are the clean ones") {
    auto withObject = solidRegion(0xFF808080U);
    for (std::size_t index = 0; index < 200; ++index) {
        withObject[index] = 0xFF101010U;
    }
    const auto clean = solidRegion(0xFF808080U);
    const auto nearlyClean = solidRegion(0xFF828282U);
    const std::vector<ImageView> regions = {viewOf(withObject), viewOf(nearlyClean), viewOf(clean)};
    REQUIRE(cleanestRegion(regions) == std::optional<std::size_t>(2));
}

TEST_CASE("a photo that sees too little of the area can't be the cleanest") {
    const auto clean = solidRegion(0xFF808080U);
    auto partial = solidRegion(0xFF808080U);
    for (std::size_t index = 40; index < partial.size(); ++index) {
        partial[index] = 0U;
    }
    const auto noisy = solidRegion(0xFF848484U);
    const std::vector<ImageView> regions = {viewOf(partial), viewOf(clean), viewOf(noisy),
                                            viewOf(clean)};
    REQUIRE(cleanestRegion(regions) == std::optional<std::size_t>(1));
}

TEST_CASE("the cleanest photo needs at least three to compare") {
    const auto clean = solidRegion(0xFF808080U);
    const std::vector<ImageView> two = {viewOf(clean), viewOf(clean)};
    REQUIRE_FALSE(cleanestRegion(two).has_value());
}

TEST_CASE("photos that see none of the area give no answer") {
    const auto empty = solidRegion(0U);
    const std::vector<ImageView> regions = {viewOf(empty), viewOf(empty), viewOf(empty)};
    REQUIRE_FALSE(cleanestRegion(regions).has_value());
}
