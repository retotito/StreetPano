#pragma once

#include <array>
#include <cmath>

namespace streetpano::core {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

[[nodiscard]] constexpr Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
[[nodiscard]] constexpr Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
[[nodiscard]] constexpr Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
[[nodiscard]] constexpr double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
[[nodiscard]] constexpr Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
[[nodiscard]] inline double length(Vec3 a) { return std::sqrt(dot(a, a)); }
[[nodiscard]] inline Vec3 normalized(Vec3 a) {
    const double size = length(a);
    return size > 0.0 ? a * (1.0 / size) : Vec3{};
}
[[nodiscard]] inline bool isFinite(Vec3 a) {
    return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}

// A 3 x 3 matrix, stored as rows.
using Mat3 = std::array<Vec3, 3>;

[[nodiscard]] constexpr Vec3 multiply(const Mat3& m, Vec3 v) {
    return {dot(m[0], v), dot(m[1], v), dot(m[2], v)};
}
[[nodiscard]] constexpr Vec3 multiplyTransposed(const Mat3& m, Vec3 v) {
    return m[0] * v.x + m[1] * v.y + m[2] * v.z;
}

}  // namespace streetpano::core
