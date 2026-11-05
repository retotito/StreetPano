#include "core/facade_surface.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <random>
#include <vector>

using Catch::Approx;
using streetpano::core::CameraPose;
using streetpano::core::FacadeSurface;
using streetpano::core::fitFacadeSurface;
using streetpano::core::Mat3;
using streetpano::core::Vec3;

namespace {

constexpr Mat3 kIdentity = {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};
constexpr double kPi = 3.14159265358979;

std::vector<CameraPose> street(int count, double spacing = 1.0, const Mat3& rotation = kIdentity) {
    std::vector<CameraPose> poses;
    for (int index = 0; index < count; ++index) {
        CameraPose pose;
        pose.center = {index * spacing, 0.0, 0.0};
        pose.worldToCamera = rotation;
        poses.push_back(pose);
    }
    return poses;
}

std::vector<Vec3> facade(int count, double depth = 15.0, double first = -10.0, double last = 40.0,
                         double scale = 1.0) {
    std::mt19937 random(1);
    std::uniform_real_distribution<double> along(first, last);
    std::uniform_real_distribution<double> height(-8.0, 2.0);
    std::vector<Vec3> points;
    for (int index = 0; index < count; ++index) {
        points.push_back(Vec3{along(random), height(random), depth} * scale);
    }
    return points;
}

void append(std::vector<Vec3>& points, const std::vector<Vec3>& more) {
    points.insert(points.end(), more.begin(), more.end());
}

Mat3 tiltedUp(double degrees) {
    const double angle = degrees * kPi / 180.0;
    return {Vec3{1, 0, 0}, Vec3{0, std::cos(angle), std::sin(angle)},
            Vec3{0, -std::sin(angle), std::cos(angle)}};
}

// The wall's depth straight ahead of a camera at this position along the street.
double depthAt(const FacadeSurface& surface, double x) {
    const auto hit = surface.hit({x, 0.0, 0.0}, {0.0, 0.0, 1.0});
    REQUIRE(hit.has_value());
    return surface.pointAt(hit->arc, 0.0).z;
}

}  // namespace

TEST_CASE("a flat facade gives a flat wall at its true distance") {
    const auto result = fitFacadeSurface(street(20), facade(2000));
    REQUIRE(result.surface.has_value());
    const auto& surface = *result.surface;
    REQUIRE(surface.medianCameraDistance == Approx(15.0).margin(0.05));
    REQUIRE(surface.up.y == Approx(-1.0));
    for (const double x : {0.0, 5.0, 10.0, 19.0}) {
        REQUIRE(depthAt(surface, x) == Approx(15.0).margin(0.05));
    }
}

TEST_CASE("the scale of the reconstruction doesn't matter") {
    constexpr double kScale = 0.01;
    const auto result =
        fitFacadeSurface(street(20, kScale), facade(2000, 15.0, -10.0, 40.0, kScale));
    REQUIRE(result.surface.has_value());
    REQUIRE(result.surface->medianCameraDistance == Approx(0.15).margin(0.001));
}

TEST_CASE("the wall follows buildings at different distances") {
    auto points = facade(1000, 15.0, -10.0, 10.0);
    append(points, facade(1000, 18.0, 15.0, 40.0));
    const auto result = fitFacadeSurface(street(30), points);
    REQUIRE(result.surface.has_value());
    REQUIRE(depthAt(*result.surface, 3.0) == Approx(15.0).margin(0.1));
    REQUIRE(depthAt(*result.surface, 25.0) == Approx(18.0).margin(0.1));
}

TEST_CASE("a change of distance is spread out, so the wall isn't torn") {
    auto points = facade(1000, 15.0, -10.0, 12.0);
    append(points, facade(1000, 20.0, 12.0, 40.0));
    const auto result = fitFacadeSurface(street(30), points);
    REQUIRE(result.surface.has_value());
    double previous = depthAt(*result.surface, 0.0);
    for (double x = 1.0; x < 29.0; x += 1.0) {
        const double depth = depthAt(*result.surface, x);
        REQUIRE(std::abs(depth - previous) < 1.0);
        previous = depth;
    }
}

TEST_CASE("a gap between buildings is bridged, not pushed back to what's behind it") {
    auto points = facade(800, 15.0, -10.0, 8.0);
    append(points, facade(800, 15.0, 20.0, 40.0));
    append(points, facade(600, 40.0, 8.0, 20.0));
    const auto result = fitFacadeSurface(street(30), points);
    REQUIRE(result.surface.has_value());
    REQUIRE(depthAt(*result.surface, 14.0) == Approx(15.0).margin(0.2));
}

TEST_CASE("a curved facade is followed and laid out at its true length") {
    // Cameras walk a circle of radius 100, and the facade is a wider circle around them.
    constexpr double kWalkRadius = 100.0;
    constexpr double kFacadeRadius = 85.0;
    constexpr int kCameras = 40;
    const double angleStep = 1.0 / kWalkRadius;
    std::vector<CameraPose> poses;
    for (int index = 0; index < kCameras; ++index) {
        const double angle = index * angleStep;
        CameraPose pose;
        pose.center = {kWalkRadius * std::sin(angle), 0.0, kWalkRadius * (1.0 - std::cos(angle))};
        // Looks towards the centre of the circle, with Y down.
        const Vec3 forward{-std::sin(angle), 0.0, std::cos(angle)};
        const Vec3 down{0.0, 1.0, 0.0};
        pose.worldToCamera = {streetpano::core::cross(down, forward), down, forward};
        poses.push_back(pose);
    }
    std::mt19937 random(2);
    std::uniform_real_distribution<double> around(-0.1, 0.5);
    std::uniform_real_distribution<double> height(-8.0, 2.0);
    std::vector<Vec3> points;
    for (int index = 0; index < 4000; ++index) {
        const double angle = around(random);
        points.push_back(Vec3{kFacadeRadius * std::sin(angle), height(random),
                              kWalkRadius - kFacadeRadius * std::cos(angle)});
    }
    const auto result = fitFacadeSurface(poses, points);
    REQUIRE(result.surface.has_value());
    const auto& surface = *result.surface;
    REQUIRE(surface.medianCameraDistance == Approx(15.0).margin(0.2));

    const auto first = surface.hit(poses[5].center, poses[5].forward());
    const auto last = surface.hit(poses[35].center, poses[35].forward());
    REQUIRE(first.has_value());
    REQUIRE(last.has_value());
    const double expected = kFacadeRadius * 30.0 * angleStep;
    REQUIRE(last->arc - first->arc == Approx(expected).epsilon(0.02));
}

TEST_CASE("objects in front of the facade don't move the wall") {
    auto points = facade(1000);
    // Cars and trees, spread at a range of nearer distances.
    std::mt19937 random(3);
    std::uniform_real_distribution<double> along(-10.0, 40.0);
    std::uniform_real_distribution<double> depth(3.0, 12.0);
    std::uniform_real_distribution<double> height(-3.0, 1.5);
    for (int index = 0; index < 1000; ++index) {
        points.push_back({along(random), height(random), depth(random)});
    }
    const auto result = fitFacadeSurface(street(20), points);
    REQUIRE(result.surface.has_value());
    REQUIRE(result.surface->medianCameraDistance == Approx(15.0).margin(0.2));
}

TEST_CASE("a road with far more points than the facade doesn't move the wall") {
    auto points = facade(500);
    std::mt19937 random(4);
    std::uniform_real_distribution<double> along(-10.0, 40.0);
    std::uniform_real_distribution<double> depth(0.5, 14.5);
    for (int index = 0; index < 5000; ++index) {
        points.push_back({along(random), 1.6, depth(random)});
    }
    const auto result = fitFacadeSurface(street(20), points);
    REQUIRE(result.surface.has_value());
    REQUIRE(result.surface->medianCameraDistance == Approx(15.0).margin(0.2));
}

TEST_CASE("a camera tilted up keeps the walls vertical") {
    const auto result = fitFacadeSurface(street(20, 1.0, tiltedUp(12.0)), facade(2000));
    REQUIRE(result.surface.has_value());
    const auto& up = result.surface->up;
    REQUIRE(std::abs(up.y) > 0.9);
}

TEST_CASE("walking the other way gives the same panorama direction") {
    auto forwards = street(20);
    auto backwards = forwards;
    std::reverse(backwards.begin(), backwards.end());
    const auto a = fitFacadeSurface(forwards, facade(2000));
    const auto b = fitFacadeSurface(backwards, facade(2000));
    REQUIRE(a.surface.has_value());
    REQUIRE(b.surface.has_value());
    const auto start = a.surface->nodes[1].point;
    const auto end = a.surface->nodes[a.surface->nodes.size() - 2].point;
    REQUIRE(streetpano::core::dot(
                end - start, b.surface->nodes.back().point - b.surface->nodes.front().point) > 0.0);
}

TEST_CASE("one camera gives a usable wall") {
    const auto result = fitFacadeSurface(street(1), facade(2000));
    REQUIRE(result.surface.has_value());
    REQUIRE(result.surface->nodes.size() == 3);
}

TEST_CASE("two cameras at one spot don't break the wall") {
    auto poses = street(3);
    poses[1].center = poses[0].center;
    const auto result = fitFacadeSurface(poses, facade(2000));
    REQUIRE(result.surface.has_value());
    for (std::size_t index = 1; index < result.surface->nodes.size(); ++index) {
        REQUIRE(result.surface->nodes[index].arc > result.surface->nodes[index - 1].arc);
    }
}

TEST_CASE("a camera with no nearby points takes its distance from its neighbours") {
    auto points = facade(800, 15.0, -10.0, 8.0);
    append(points, facade(800, 15.0, 20.0, 40.0));
    const auto result = fitFacadeSurface(street(30), points);
    REQUIRE(result.surface.has_value());
    REQUIRE(depthAt(*result.surface, 14.0) == Approx(15.0).margin(0.2));
}

TEST_CASE("cameras beyond the last points keep the nearest distance") {
    const auto points = facade(800, 15.0, 10.0, 20.0);
    const auto result = fitFacadeSurface(street(30), points);
    REQUIRE(result.surface.has_value());
    REQUIRE(depthAt(*result.surface, 1.0) == Approx(15.0).margin(0.2));
    REQUIRE(depthAt(*result.surface, 28.0) == Approx(15.0).margin(0.2));
}

TEST_CASE("the wall can't be reached from behind or along its length") {
    const auto result = fitFacadeSurface(street(20), facade(2000));
    REQUIRE(result.surface.has_value());
    REQUIRE_FALSE(result.surface->hit({5.0, 0.0, 0.0}, {0.0, 0.0, -1.0}).has_value());
    REQUIRE_FALSE(result.surface->hit({5.0, 0.0, 0.0}, {1.0, 0.0, 0.0}).has_value());
    REQUIRE_FALSE(result.surface->hit({5.0, 0.0, 30.0}, {0.0, 0.0, 1.0}).has_value());
}

TEST_CASE("a ray's height on the wall is measured along up") {
    const auto result = fitFacadeSurface(street(20), facade(2000));
    REQUIRE(result.surface.has_value());
    // Up is -Y here, so a ray rising at 1 in 15 reaches height 1 at the facade.
    const auto hit = result.surface->hit({5.0, 0.0, 0.0}, {0.0, -1.0, 15.0});
    REQUIRE(hit.has_value());
    REQUIRE(hit->height == Approx(1.0).margin(0.01));
}

TEST_CASE("unusable input gives clear errors") {
    SECTION("no cameras") {
        const auto result = fitFacadeSurface({}, facade(500));
        REQUIRE_FALSE(result.surface.has_value());
        REQUIRE(result.error.find("no camera") != std::string::npos);
    }
    SECTION("too few points") {
        const auto result = fitFacadeSurface(street(5), facade(5));
        REQUIRE_FALSE(result.surface.has_value());
        REQUIRE(result.error.find("too few") != std::string::npos);
    }
    SECTION("all points behind the cameras") {
        const auto result = fitFacadeSurface(street(5), facade(500, -15.0));
        REQUIRE_FALSE(result.surface.has_value());
        REQUIRE(result.error.find("in front") != std::string::npos);
    }
    SECTION("points too far along the street to be near any camera") {
        const auto result = fitFacadeSurface(street(5), facade(500, 15.0, 1000.0, 1001.0));
        REQUIRE_FALSE(result.surface.has_value());
    }
    SECTION("cameras looking straight up") {
        const Mat3 upwards = {Vec3{1, 0, 0}, Vec3{0, 0, 1}, Vec3{0, -1, 0}};
        auto poses = street(5, 1.0, upwards);
        for (auto& pose : poses) {
            pose.upInCamera = {0.0, 0.0, 1.0};
        }
        const auto result = fitFacadeSurface(poses, facade(500));
        REQUIRE_FALSE(result.surface.has_value());
        REQUIRE(result.error.find("straight at") != std::string::npos);
    }
}
