#include "core/seam_match.hpp"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <vector>

using Catch::Approx;
using streetpano::core::CameraPose;
using streetpano::core::FacadeSurface;
using streetpano::core::ImageView;
using streetpano::core::Mat3;
using streetpano::core::matchAtSeam;
using streetpano::core::sampleOffsets;
using streetpano::core::SeamOffset;
using streetpano::core::SeamPhoto;
using streetpano::core::Vec3;
using streetpano::core::withDepthOffsets;

namespace {

constexpr Mat3 kIdentity = {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};
constexpr double kFocal = 400.0;
constexpr double kTrueDepth = 15.0;
constexpr int kWidth = 480;
constexpr int kHeight = 480;

// A fine pattern, because a wrong distance shifts the photos by only about 0.03 units here.
std::uint32_t wallPixel(double x, double y) {
    const double value = 127.0 + 60.0 * std::sin(x * 23.0 + 1.7 * std::sin(y * 5.0)) +
                         60.0 * std::sin(y * 19.0 + 0.9 * std::sin(x * 4.0));
    const auto green = static_cast<std::uint32_t>(std::clamp(value, 0.0, 255.0));
    return 0xFF000000U | (green << 16U) | (green << 8U) | green;
}

struct Photo {
    CameraPose pose;
    std::vector<std::uint32_t> pixels;

    [[nodiscard]] SeamPhoto seamPhoto() const {
        return {&pose, ImageView{pixels.data(), kWidth, kHeight, kWidth * sizeof(std::uint32_t)}};
    }
};

Photo photoAt(double x, bool plain = false) {
    Photo photo;
    photo.pose.center = {x, 0.0, 0.0};
    photo.pose.worldToCamera = kIdentity;
    photo.pixels.resize(kWidth * kHeight);
    for (int v = 0; v < kHeight; ++v) {
        for (int u = 0; u < kWidth; ++u) {
            const double wallX = x + (u + 0.5 - kWidth / 2.0) * kTrueDepth / kFocal;
            const double wallY = (v + 0.5 - kHeight / 2.0) * kTrueDepth / kFocal;
            photo.pixels[static_cast<std::size_t>(v * kWidth + u)] =
                plain ? 0xFF808080U : wallPixel(wallX, wallY);
        }
    }
    return photo;
}

// A straight wall at this depth, with a sample at each of the given cameras.
FacadeSurface wallAt(double depth, int cameras = 6) {
    FacadeSurface surface;
    surface.up = {0.0, -1.0, 0.0};
    surface.medianCameraDistance = depth;
    surface.nodes.push_back({Vec3{-100.0, 0.0, depth}, 0.0});
    for (int index = 0; index < cameras; ++index) {
        surface.nodes.push_back({Vec3{static_cast<double>(index), 0.0, depth}, 100.0 + index});
        surface.samples.push_back(
            {Vec3{static_cast<double>(index), 0.0, 0.0}, Vec3{1, 0, 0}, depth, 100.0 + index});
    }
    surface.nodes.push_back({Vec3{100.0, 0.0, depth}, 200.0});
    return surface;
}

constexpr double kSeamArc = 102.5;  // Half way between cameras at x = 2 and x = 3.

}  // namespace

TEST_CASE("a wall too near is moved back to where the photos agree") {
    const auto first = photoAt(2.0);
    const auto second = photoAt(3.0);
    const auto match =
        matchAtSeam(wallAt(14.5), kSeamArc, first.seamPhoto(), second.seamPhoto(), kFocal);
    REQUIRE(match.offset.has_value());
    REQUIRE(*match.offset == Approx(0.5).margin(0.15));
    REQUIRE(match.bestScore > match.scoreAtWall);
}

TEST_CASE("a wall too far is moved forward") {
    const auto first = photoAt(2.0);
    const auto second = photoAt(3.0);
    const auto match =
        matchAtSeam(wallAt(15.5), kSeamArc, first.seamPhoto(), second.seamPhoto(), kFocal);
    REQUIRE(match.offset.has_value());
    REQUIRE(*match.offset == Approx(-0.5).margin(0.15));
}

TEST_CASE("a wall in the right place stays there") {
    const auto first = photoAt(2.0);
    const auto second = photoAt(3.0);
    const auto match =
        matchAtSeam(wallAt(kTrueDepth), kSeamArc, first.seamPhoto(), second.seamPhoto(), kFocal);
    REQUIRE(match.scoreAtWall > 0.9);
    REQUIRE_FALSE(match.offset.has_value());
}

TEST_CASE("a plain wall gives no answer") {
    const auto first = photoAt(2.0, true);
    const auto second = photoAt(3.0, true);
    const auto match =
        matchAtSeam(wallAt(14.5), kSeamArc, first.seamPhoto(), second.seamPhoto(), kFocal);
    REQUIRE_FALSE(match.offset.has_value());
}

TEST_CASE("photos that don't see the seam give no answer") {
    const auto first = photoAt(2.0);
    const auto second = photoAt(3.0);
    const auto match =
        matchAtSeam(wallAt(14.5), 190.0, first.seamPhoto(), second.seamPhoto(), kFocal);
    REQUIRE_FALSE(match.offset.has_value());
}

TEST_CASE("a seam behind the cameras gives no answer") {
    auto first = photoAt(2.0);
    auto second = photoAt(3.0);
    first.pose.center.z = 200.0;
    second.pose.center.z = 200.0;
    const auto match =
        matchAtSeam(wallAt(14.5), kSeamArc, first.seamPhoto(), second.seamPhoto(), kFocal);
    REQUIRE_FALSE(match.offset.has_value());
}

TEST_CASE("unrelated photos give no answer") {
    const auto first = photoAt(2.0);
    const auto second = photoAt(40.0);
    auto moved = second;
    moved.pose.center = {3.0, 0.0, 0.0};
    const auto match =
        matchAtSeam(wallAt(14.5), kSeamArc, first.seamPhoto(), moved.seamPhoto(), kFocal);
    REQUIRE_FALSE(match.offset.has_value());
}

TEST_CASE("a seam moves the samples on either side of it") {
    const SeamOffset seams[] = {{kSeamArc, 0.3}};
    const auto offsets = sampleOffsets(wallAt(15.0), seams);
    REQUIRE(offsets.size() == 6);
    REQUIRE(offsets[1] == 0.0);
    REQUIRE(offsets[2] == Approx(0.3));
    REQUIRE(offsets[3] == Approx(0.3));
    REQUIRE(offsets[4] == 0.0);
}

TEST_CASE("two seams beside one sample are averaged") {
    const SeamOffset seams[] = {{101.5, 0.2}, {102.5, 0.4}};
    const auto offsets = sampleOffsets(wallAt(15.0), seams);
    REQUIRE(offsets[2] == Approx(0.3));
}

TEST_CASE("seams beyond the ends move the nearest sample") {
    const SeamOffset seams[] = {{50.0, 0.2}, {150.0, 0.4}};
    const auto offsets = sampleOffsets(wallAt(15.0), seams);
    REQUIRE(offsets.front() == Approx(0.2));
    REQUIRE(offsets.back() == Approx(0.4));
}

TEST_CASE("offsets move the wall") {
    const auto surface = wallAt(15.0, 12);
    const std::vector<double> offsets(12, 0.5);
    const auto moved = withDepthOffsets(surface, offsets);
    REQUIRE(moved.samples.size() == 12);
    REQUIRE(moved.medianCameraDistance == Approx(15.5));
    REQUIRE(moved.samples[6].depth == Approx(15.5));
}

TEST_CASE("one wrong offset among good ones is smoothed away") {
    const auto surface = wallAt(15.0, 12);
    std::vector<double> offsets(12, 0.0);
    offsets[6] = 3.0;
    const auto moved = withDepthOffsets(surface, offsets);
    REQUIRE(moved.samples[6].depth == Approx(15.0));
}
