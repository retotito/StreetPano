#pragma once

#include <QImage>
#include <QString>
#include <optional>
#include <vector>

#include "core/patch.hpp"

namespace streetpano::stitch {

// The user's edits, in their own file. Photos and camera positions are never changed, so deleting
// or editing a decision is always possible.
struct Decisions {
    std::vector<core::Patch> patches;
};

struct DecisionsResult {
    std::optional<Decisions> decisions;
    QString error;
};

// Where decisions belong: beside the camera positions.
[[nodiscard]] QString decisionsPathFor(const QString& posesPath);

// A missing file gives empty decisions, because that's a project with no edits yet.
[[nodiscard]] DecisionsResult readDecisions(const QString& path);

// These write to a temporary file and then swap it into place, so that a crash or a full disk
// can't damage the existing file. They return false if nothing was written.
[[nodiscard]] bool writeDecisions(const Decisions& decisions, const QString& path);
[[nodiscard]] bool savePanorama(const QImage& panorama, const QString& path);

}  // namespace streetpano::stitch
