#include "core/street_frame.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <random>
#include <vector>

using Catch::Approx;
using streetpano::core::CameraPose;
using streetpano::core::dot;
using streetpano::core::fitStreetFrame;
using streetpano::core::Mat3;
using streetpano::core::smallestEigenvector;
using streetpano::core::Vec3;

namespace {

constexpr Mat3 kIdentity = {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};

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

std::vector<Vec3> facade(int count, double depth = 15.0, double scale = 1.0) {
    std::mt19937 random(1);
    std::uniform_real_distribution<double> along(-10.0, 40.0);
    std::uniform_real_distribution<double> height(-8.0, 2.0);
    std::vector<Vec3> points;
    for (int index = 0; index < count; ++index) {
        points.push_back(Vec3{along(random), height(random), depth} * scale);
    }
    return points;
}

Mat3 tiltedUp(double degrees) {
    const double angle = degrees * 3.14159265358979 / 180.0;
    return {Vec3{1, 0, 0}, Vec3{0, std::cos(angle), std::sin(angle)},
            Vec3{0, -std::sin(angle), std::cos(angle)}};
}

}  // namespace

TEST_CASE("the facade and its axes are recovered") {
    const auto result = fitStreetFrame(street(20), facade(500));
    REQUIRE(result.frame.has_value());
    const auto& frame = *result.frame;
    REQUIRE(frame.normal.z == Approx(1.0).margin(1e-6));
    REQUIRE(frame.right.x == Approx(1.0).margin(1e-6));
    REQUIRE(frame.up.y == Approx(-1.0).margin(1e-6));
    REQUIRE(frame.medianCameraDistance == Approx(15.0).margin(1e-6));
    REQUIRE(frame.inlierCount == 500);
}

TEST_CASE("the result doesn't depend on the reconstruction's scale") {
    constexpr double kScale = 0.07;
    const auto result = fitStreetFrame(street(20, kScale), facade(500, 15.0, kScale));
    REQUIRE(result.frame.has_value());
    REQUIRE(result.frame->normal.z == Approx(1.0).margin(1e-6));
    REQUIRE(result.frame->medianCameraDistance == Approx(15.0 * kScale).margin(1e-6));
}

TEST_CASE("cars, trees and the ground don't move the facade") {
    std::vector<Vec3> points = facade(600);
    std::mt19937 random(2);
    std::uniform_real_distribution<double> along(-10.0, 40.0);
    std::uniform_real_distribution<double> depth(1.0, 14.0);
    std::uniform_real_distribution<double> height(-8.0, 2.0);
    for (int index = 0; index < 200; ++index) {
        points.push_back({along(random), 1.6, depth(random)});             // Ground.
        points.push_back({along(random), height(random), depth(random)});  // Scattered objects.
    }
    const auto result = fitStreetFrame(street(20), points);
    REQUIRE(result.frame.has_value());
    REQUIRE(result.frame->normal.z == Approx(1.0).margin(1e-3));
    REQUIRE(result.frame->medianCameraDistance == Approx(15.0).margin(0.05));
}

TEST_CASE("the ground is not chosen as the facade even when it has the most points") {
    std::vector<Vec3> points = facade(100);
    std::mt19937 random(3);
    std::uniform_real_distribution<double> along(-10.0, 40.0);
    std::uniform_real_distribution<double> depth(1.0, 15.0);
    for (int index = 0; index < 1000; ++index) {
        points.push_back({along(random), 1.6, depth(random)});
    }
    const auto result = fitStreetFrame(street(20), points);
    REQUIRE(result.frame.has_value());
    REQUIRE(std::abs(result.frame->normal.z) == Approx(1.0).margin(1e-3));
}

TEST_CASE("tilting the camera up keeps the facade's verticals") {
    const auto result = fitStreetFrame(street(20, 1.0, tiltedUp(12.0)), facade(500));
    REQUIRE(result.frame.has_value());
    REQUIRE(result.frame->up.y == Approx(-1.0).margin(1e-6));
    REQUIRE(result.frame->normal.z == Approx(1.0).margin(1e-6));
}

TEST_CASE("the panorama reads the same whichever way the street was walked") {
    const auto forwards = fitStreetFrame(street(20, 1.0), facade(500));
    const auto backwards = fitStreetFrame(street(20, -1.0), facade(500));
    REQUIRE(forwards.frame.has_value());
    REQUIRE(backwards.frame.has_value());
    REQUIRE(dot(forwards.frame->right, backwards.frame->right) == Approx(1.0));
}

TEST_CASE("a phone held another way round still gives the right up direction") {
    // Stored pixels rotated by 90 degrees, as recorded by the photo's orientation tag.
    const Mat3 rotated = {Vec3{0, -1, 0}, Vec3{1, 0, 0}, Vec3{0, 0, 1}};
    auto poses = street(20, 1.0, rotated);
    for (auto& pose : poses) {
        pose.upInCamera = {1.0, 0.0, 0.0};
    }
    const auto result = fitStreetFrame(poses, facade(500));
    REQUIRE(result.frame.has_value());
    REQUIRE(result.frame->up.y == Approx(-1.0).margin(1e-6));
    REQUIRE(result.frame->right.x == Approx(1.0).margin(1e-6));
}

TEST_CASE("problems give clear errors") {
    SECTION("no cameras") {
        const auto result = fitStreetFrame({}, facade(500));
        REQUIRE_FALSE(result.frame.has_value());
        REQUIRE(result.error.find("no camera") != std::string::npos);
    }
    SECTION("too few points") {
        const auto result = fitStreetFrame(street(5), facade(5));
        REQUIRE_FALSE(result.frame.has_value());
        REQUIRE(result.error.find("too few") != std::string::npos);
    }
    SECTION("no plane faces the cameras") {
        std::vector<Vec3> ground;
        std::mt19937 random(4);
        std::uniform_real_distribution<double> value(0.0, 30.0);
        for (int index = 0; index < 300; ++index) {
            ground.push_back({value(random), 1.6, value(random)});
        }
        const auto result = fitStreetFrame(street(20), ground);
        REQUIRE_FALSE(result.frame.has_value());
        REQUIRE(result.error.find("No plane") != std::string::npos);
    }
}

TEST_CASE("the smallest eigenvector is found") {
    const Mat3 diagonal = {Vec3{5, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 9}};
    REQUIRE(std::abs(smallestEigenvector(diagonal).y) == Approx(1.0));

    const Mat3 general = {Vec3{2, 1, 0}, Vec3{1, 2, 0}, Vec3{0, 0, 7}};
    const Vec3 vector = smallestEigenvector(general);
    REQUIRE(std::abs(vector.x + vector.y) == Approx(0.0).margin(1e-9));
    REQUIRE(std::abs(vector.z) == Approx(0.0).margin(1e-9));
}
