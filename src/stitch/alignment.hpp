#pragma once

#include <QDir>
#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/camera_pose.hpp"
#include "core/facade_surface.hpp"
#include "core/stitch_plan.hpp"
#include "stitch/pose_file.hpp"

namespace streetpano::stitch {

// The street's geometry, worked out from the camera positions but not stitched yet. Aligning and
// stitching both start from this.
struct PreparedStreet {
    std::vector<core::CameraPose> poses;  // Only photos that could be read, in their own order.
    QSize photoSize;
    double focalPixels = 0.0;
    core::FacadeSurface surface;
    core::StitchPlan plan;
    QStringList warnings;
    QString error;  // Set if nothing else is usable.
};

[[nodiscard]] PreparedStreet prepareStreet(const PoseFile& poses, const QDir& photoFolder,
                                           const core::StitchOptions& options = {});

// One photo in the side-by-side view.
struct AlignmentTile {
    std::string photo;
    int left = 0;  // In the strip.
    int width = 0;
};

// Every photo projected onto the wall at the same scale and height, side by side with gaps, so
// that a horizontal line means the same height in all of them.
struct AlignmentView {
    QImage strip;
    std::vector<AlignmentTile> tiles;
    double unitsPerPixel = 0.0;  // On the wall.
    double topHeight = 0.0;      // On the wall, at the strip's top row.
    QStringList warnings;
    QString error;

    [[nodiscard]] double rowForHeight(double height) const {
        return (topHeight - height) / unitsPerPixel;
    }
    [[nodiscard]] double heightForRow(double row) const { return topHeight - row * unitsPerPixel; }
};

using AlignmentProgress = std::function<void(int done, int total)>;

// Builds the side-by-side view, stripHeight pixels tall. Reads one photo at a time.
[[nodiscard]] AlignmentView buildAlignmentView(const PreparedStreet& street,
                                               const QDir& photoFolder, int stripHeight,
                                               const AlignmentProgress& progress = {},
                                               const std::atomic<bool>* cancel = nullptr);

// The user's guide lines, as heights on the wall, so they survive a change of view size.
struct Alignment {
    std::vector<double> guides;
};

struct AlignmentResult {
    std::optional<Alignment> alignment;
    QString error;
};

[[nodiscard]] QString alignmentPathFor(const QString& posesPath);
// A missing file gives no guides, because that's a street that hasn't been aligned yet.
[[nodiscard]] AlignmentResult readAlignment(const QString& path);
// Writes to a temporary file and swaps it into place.
[[nodiscard]] bool writeAlignment(const Alignment& alignment, const QString& path);

}  // namespace streetpano::stitch
