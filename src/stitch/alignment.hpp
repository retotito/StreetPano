#pragma once

#include <QDir>
#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/camera_pose.hpp"
#include "core/facade_surface.hpp"
#include "core/photo_adjustment.hpp"
#include "core/stitch_plan.hpp"
#include "stitch/pose_file.hpp"

namespace streetpano::stitch {

// The user's guide lines, as heights on the wall, so they survive a change of view size, and the
// corrections made to single photos, by photo name.
struct Alignment {
    std::vector<double> guides;
    std::map<std::string, core::PhotoAdjustment> photos;

    [[nodiscard]] core::PhotoAdjustment adjustmentFor(const std::string& photo) const {
        const auto found = photos.find(photo);
        return found == photos.end() ? core::PhotoAdjustment{} : found->second;
    }
};

// The street's geometry, worked out from the camera positions but not stitched yet. Aligning and
// stitching both start from this.
struct PreparedStreet {
    // Only photos that could be read, in their own order, with the user's corrections.
    std::vector<core::CameraPose> poses;
    std::vector<core::CameraPose> originalPoses;  // The same photos, without corrections.
    QSize photoSize;
    double focalPixels = 0.0;
    core::FacadeSurface surface;
    core::StitchPlan plan;
    QStringList warnings;
    QString error;  // Set if nothing else is usable.
};

// The wall is fitted to the original camera positions, so that correcting one photo can't move the
// wall under the others.
[[nodiscard]] PreparedStreet prepareStreet(const PoseFile& poses, const QDir& photoFolder,
                                           const core::StitchOptions& options = {},
                                           const Alignment* alignment = nullptr);

// One photo in the side-by-side view.
struct AlignmentTile {
    std::string photo;
    std::size_t camera = 0;  // In the prepared street.
    int left = 0;            // In the strip.
    int width = 0;
    int column = 0;  // The panorama column, at the strip's scale, that the tile starts at.
};

// Every photo projected onto the wall at the same scale and height, side by side with gaps, so
// that a horizontal line means the same height in all of them.
struct AlignmentView {
    QImage strip;
    QImage original;  // The same tiles, showing each photo as it was taken.
    std::vector<AlignmentTile> tiles;
    core::StitchPlan plan;       // The panorama's plan at the strip's scale.
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

struct AlignmentResult {
    std::optional<Alignment> alignment;
    QString error;
};

// Draws one tile again with a correction, into the same place in the strip.
void redrawTile(AlignmentView& view, const PreparedStreet& street, const QDir& photoFolder,
                std::size_t tile, const core::PhotoAdjustment& adjustment);

// Returns the tile at this strip column, or -1 in a gap.
[[nodiscard]] int tileAt(const AlignmentView& view, double column);

[[nodiscard]] QString alignmentPathFor(const QString& posesPath);
// A missing file gives no guides, because that's a street that hasn't been aligned yet.
[[nodiscard]] AlignmentResult readAlignment(const QString& path);
// Writes to a temporary file and swaps it into place.
[[nodiscard]] bool writeAlignment(const Alignment& alignment, const QString& path);

}  // namespace streetpano::stitch
