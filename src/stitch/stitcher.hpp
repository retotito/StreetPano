#pragma once

#include <QDir>
#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/facade_surface.hpp"
#include "core/patch.hpp"
#include "core/stitch_plan.hpp"
#include "stitch/alignment.hpp"
#include "stitch/pose_file.hpp"

namespace streetpano::stitch {

// How the wall was adjusted to make neighbouring photos agree.
struct SeamAlignment {
    int seams = 0;
    int moved = 0;
    double meanScoreBefore = 0.0;  // From -1 to 1.
};

// A finished stitch, with what's needed to add patches later. Decisions aren't part of base, so
// that any of them can be changed by drawing the others again.
struct StitchResult {
    QImage panorama;  // base with the patches drawn on it.
    QImage base;
    QString error;
    QStringList warnings;
    SeamAlignment alignment;
    double scale = 1.0;
    core::FacadeSurface surface;
    core::StitchPlan plan;
    std::vector<core::CameraPose> poses;
    double focalPixels = 0.0;
    QSize photoSize;
};

// Called after each photo, with the number done and the total.
using StitchProgress = std::function<void(int done, int total)>;

// Builds a panorama, holding only the canvas and one photo in memory at a time.
// Photos are read without applying their orientation tags, because COLMAP ignores them too.
// This is safe to run on a worker thread. Setting cancel stops it after the current photo.
[[nodiscard]] StitchResult stitchPanorama(const PoseFile& poses, const QDir& photoFolder,
                                          const core::StitchOptions& options = {},
                                          const StitchProgress& progress = {},
                                          const std::atomic<bool>* cancel = nullptr,
                                          const Alignment* alignment = nullptr);

// Returns base with these patches drawn on it in order, reading each photo from its original
// file. A patch whose photo can't be used is skipped, with a warning.
[[nodiscard]] QImage withPatches(const StitchResult& stitched, const QDir& photoFolder,
                                 std::span<const core::Patch> patches, QStringList& warnings,
                                 int featherPixels = core::StitchOptions{}.featherPixels);

// Returns the photo that shows an area cleanest, to remove what's in it, from those that can see
// it. Returns nothing if there are too few photos to compare.
[[nodiscard]] std::optional<std::string> cleanestPhoto(const StitchResult& stitched,
                                                       const QDir& photoFolder,
                                                       std::span<const core::WallPoint> outline);

}  // namespace streetpano::stitch
