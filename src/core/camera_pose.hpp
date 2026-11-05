#pragma once

#include <string>

#include "core/vec3.hpp"

namespace streetpano::core {

// One photo's position, in COLMAP's convention: the camera looks along +Z with X to the
// right and Y down. Reconstructions have an arbitrary scale, so nothing here assumes metres.
struct CameraPose {
    std::string name;
    Vec3 center;
    Mat3 worldToCamera{};
    // Which way is up in the stored pixels. It differs for a phone held another way round.
    Vec3 upInCamera{0.0, -1.0, 0.0};

    [[nodiscard]] Vec3 forward() const {
        return multiplyTransposed(worldToCamera, {0.0, 0.0, 1.0});
    }
    [[nodiscard]] Vec3 up() const { return multiplyTransposed(worldToCamera, upInCamera); }
};

}  // namespace streetpano::core
