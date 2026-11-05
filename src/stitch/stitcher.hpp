#pragma once

#include <QDir>
#include <QImage>
#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>

#include "core/stitch_plan.hpp"
#include "stitch/pose_file.hpp"

namespace streetpano::stitch {

struct StitchResult {
    QImage panorama;
    QString error;
    QStringList warnings;
    double scale = 1.0;
};

// Called after each photo, with the number done and the total.
using StitchProgress = std::function<void(int done, int total)>;

// Builds a panorama, holding only the canvas and one photo in memory at a time.
// Photos are read without applying their orientation tags, because COLMAP ignores them too.
// This is safe to run on a worker thread. Setting cancel stops it after the current photo.
[[nodiscard]] StitchResult stitchPanorama(const PoseFile& poses, const QDir& photoFolder,
                                          const core::StitchOptions& options = {},
                                          const StitchProgress& progress = {},
                                          const std::atomic<bool>* cancel = nullptr);

}  // namespace streetpano::stitch
