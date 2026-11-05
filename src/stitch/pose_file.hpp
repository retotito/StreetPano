#pragma once

#include <QString>
#include <optional>
#include <vector>

#include "core/street_frame.hpp"

namespace streetpano::stitch {

// Camera positions and 3D points, as written by the developer tools.
struct PoseFile {
    double focalPixels = 0.0;
    std::vector<core::CameraPose> poses;
    std::vector<core::Vec3> points;
};

struct PoseFileResult {
    std::optional<PoseFile> file;
    QString error;
};

// Checks everything it reads, so that a damaged file gives an error instead of bad geometry.
[[nodiscard]] PoseFileResult readPoseFile(const QString& path);

}  // namespace streetpano::stitch
