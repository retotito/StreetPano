#include "stitch/alignment.hpp"

#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

namespace streetpano::stitch {
namespace {

constexpr int kGapPixels = 12;
constexpr double kSharpness = 1.5;  // Photos are read a little larger than shown, to stay sharp.
constexpr int kFormatVersion = 1;
constexpr qint64 kMaxFileBytes = qint64{4} * 1024 * 1024;
constexpr QRgb kGapColour = qRgb(32, 34, 37);

[[nodiscard]] QString photoPath(const QDir& folder, const core::CameraPose& pose) {
    return folder.filePath(QString::fromStdString(pose.name));
}

// Reads a photo at about the size it's shown, and projects it into its tile.
void drawTile(AlignmentView& view, const PreparedStreet& street, const QDir& photoFolder,
              const AlignmentTile& tile, const core::CameraPose& pose) {
    const double fullUnitsPerPixel = street.surface.medianCameraDistance / street.focalPixels;
    const double shrink = std::min(1.0, kSharpness * fullUnitsPerPixel / view.plan.unitsPerPixel);
    const QSize readSize(std::max(2, static_cast<int>(street.photoSize.width() * shrink)),
                         std::max(2, static_cast<int>(street.photoSize.height() * shrink)));
    const double focal =
        street.focalPixels * readSize.width() / static_cast<double>(street.photoSize.width());
    QPainter painter(&view.strip);
    painter.fillRect(tile.left, 0, tile.width, view.strip.height(), kGapColour);
    QImageReader reader(photoPath(photoFolder, pose));
    reader.setAutoTransform(false);
    reader.setScaledSize(readSize);
    QImage photo = reader.read();
    if (photo.isNull()) {
        view.warnings.append(QStringLiteral("Skipped %1: %2")
                                 .arg(QString::fromStdString(pose.name), reader.errorString()));
        return;
    }
    photo.convertTo(QImage::Format_ARGB32);
    const QImage& source = photo;
    const core::ImageView sourceView{reinterpret_cast<const std::uint32_t*>(source.constBits()),
                                     source.width(), source.height(),
                                     static_cast<std::size_t>(source.bytesPerLine())};
    const std::vector<std::uint32_t> pixels =
        core::projectPhoto(street.surface, view.plan, pose, focal, sourceView, tile.column,
                           tile.column + tile.width, 0, view.strip.height());
    const QImage projected(reinterpret_cast<const uchar*>(pixels.data()), tile.width,
                           view.strip.height(), static_cast<qsizetype>(tile.width) * 4,
                           QImage::Format_ARGB32);
    painter.drawImage(tile.left, 0, projected);
}

// Draws a photo as it was taken, scaled to the strip's height and centred in its tile.
void drawOriginal(AlignmentView& view, const QDir& photoFolder, const AlignmentTile& tile) {
    QImageReader reader(photoFolder.filePath(QString::fromStdString(tile.photo)));
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (!size.isValid()) {
        return;
    }
    // The size before turning; a photo held upright is reported on its side.
    const bool turned = reader.transformation().testFlag(QImageIOHandler::TransformationRotate90);
    const QSize upright = turned ? size.transposed() : size;
    const int height = view.original.height();
    const int width = std::max(1, upright.width() * height / std::max(1, upright.height()));
    reader.setScaledSize(turned ? QSize(height, width) : QSize(width, height));
    const QImage photo = reader.read();
    QPainter painter(&view.original);
    painter.setClipRect(tile.left, 0, tile.width, height);
    painter.drawImage(tile.left + (tile.width - photo.width()) / 2, 0, photo);
}

}  // namespace

PreparedStreet prepareStreet(const PoseFile& poses, const QDir& photoFolder,
                             const core::StitchOptions& options, const Alignment* alignment) {
    PreparedStreet street;
    street.focalPixels = poses.focalPixels;
    // Unreadable photos are left out of the plan, so that their neighbours fill the gap.
    street.poses.reserve(poses.poses.size());
    for (const core::CameraPose& pose : poses.poses) {
        QImageReader reader(photoPath(photoFolder, pose));
        reader.setAutoTransform(false);
        const QSize size = reader.size();
        if (!size.isValid()) {
            street.warnings.append(
                QStringLiteral("Skipped %1: %2")
                    .arg(QString::fromStdString(pose.name), reader.errorString()));
            continue;
        }
        if (street.photoSize.isValid() && size != street.photoSize) {
            street.warnings.append(QStringLiteral("Skipped %1: it has a different size.")
                                       .arg(QString::fromStdString(pose.name)));
            continue;
        }
        street.photoSize = size;
        street.poses.push_back(pose);
    }
    if (street.poses.empty()) {
        street.error = QStringLiteral("None of the photos could be read.");
        return street;
    }
    const core::SurfaceResult surface = core::fitFacadeSurface(street.poses, poses.points);
    if (!surface.surface.has_value()) {
        street.error = QString::fromStdString(surface.error);
        return street;
    }
    street.surface = *surface.surface;
    street.originalPoses = street.poses;
    if (alignment != nullptr) {
        for (core::CameraPose& pose : street.poses) {
            pose = core::adjustedPose(pose, alignment->adjustmentFor(pose.name), street.surface);
        }
    }
    const core::PlanResult plan =
        core::makeStitchPlan(street.surface, street.poses, poses.focalPixels,
                             street.photoSize.width(), street.photoSize.height(), options);
    if (!plan.plan.has_value()) {
        street.error = QString::fromStdString(plan.error);
        return street;
    }
    street.plan = *plan.plan;
    return street;
}

AlignmentView buildAlignmentView(const PreparedStreet& street, const QDir& photoFolder,
                                 int stripHeight, const AlignmentProgress& progress,
                                 const std::atomic<bool>* cancel) {
    AlignmentView view;
    if (!street.error.isEmpty() || street.plan.height <= 0 || stripHeight <= 0) {
        view.error =
            street.error.isEmpty() ? QStringLiteral("There's nothing to align.") : street.error;
        return view;
    }
    // The same plan as the panorama, but at the strip's size.
    core::StitchPlan small = street.plan;
    small.unitsPerPixel = street.plan.unitsPerPixel * street.plan.height / stripHeight;
    small.height = stripHeight;
    view.plan = small;
    view.unitsPerPixel = small.unitsPerPixel;
    view.topHeight = small.topHeight;

    // Tiles are laid out from the original positions, so a correction never moves its slot.
    int width = 0;
    for (std::size_t index = 0; index < street.originalPoses.size(); ++index) {
        const auto box =
            core::photoBox(street.surface, small, street.originalPoses[index], street.focalPixels,
                           street.photoSize.width(), street.photoSize.height());
        if (!box.has_value()) {
            view.warnings.append(QStringLiteral("%1 doesn't look at the facade.")
                                     .arg(QString::fromStdString(street.poses[index].name)));
            continue;
        }
        const int tileWidth = box->right - box->left;
        view.tiles.push_back(
            AlignmentTile{street.poses[index].name, index, width, tileWidth, box->left});
        width += tileWidth + kGapPixels;
    }
    if (view.tiles.empty()) {
        view.error = QStringLiteral("No photo looks at the facade.");
        return view;
    }
    view.strip = QImage(width, stripHeight, QImage::Format_ARGB32);
    view.original = QImage(width, stripHeight, QImage::Format_ARGB32);
    if (view.strip.isNull() || view.original.isNull()) {
        view.error = QStringLiteral("There isn't enough memory for the side-by-side view.");
        return view;
    }
    view.strip.fill(kGapColour);
    view.original.fill(kGapColour);

    int done = 0;
    for (const AlignmentTile& tile : view.tiles) {
        if (cancel != nullptr && cancel->load()) {
            view.error = QStringLiteral("Cancelled.");
            return view;
        }
        drawTile(view, street, photoFolder, tile, street.poses[tile.camera]);
        drawOriginal(view, photoFolder, tile);
        ++done;
        if (progress) {
            progress(done, static_cast<int>(view.tiles.size()));
        }
    }
    return view;
}

void redrawTile(AlignmentView& view, const PreparedStreet& street, const QDir& photoFolder,
                std::size_t tile, const core::PhotoAdjustment& adjustment) {
    if (tile >= view.tiles.size() || view.strip.isNull()) {
        return;
    }
    const AlignmentTile& placed = view.tiles[tile];
    drawTile(view, street, photoFolder, placed,
             core::adjustedPose(street.originalPoses[placed.camera], adjustment, street.surface));
}

int tileAt(const AlignmentView& view, double column) {
    for (std::size_t index = 0; index < view.tiles.size(); ++index) {
        const AlignmentTile& tile = view.tiles[index];
        if (column >= tile.left && column < tile.left + tile.width) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

QString alignmentPathFor(const QString& posesPath) {
    return QFileInfo(posesPath).absoluteDir().filePath(QStringLiteral("alignment.json"));
}

AlignmentResult readAlignment(const QString& path) {
    AlignmentResult result;
    QFile file(path);
    if (!file.exists()) {
        result.alignment = Alignment{};
        return result;
    }
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxFileBytes) {
        result.error = QStringLiteral("Can't read %1.").arg(path);
        return result;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const QJsonObject root = document.object();
    if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
        root.value(QStringLiteral("format_version")).toInt() != kFormatVersion) {
        result.error = QStringLiteral("%1 isn't a valid alignment.").arg(path);
        return result;
    }
    Alignment alignment;
    const QJsonArray saved = root.value(QStringLiteral("guides")).toArray();
    for (qsizetype index = 0; index < saved.size(); ++index) {
        const QJsonValue guide = saved.at(index);
        if (!guide.isDouble() || !std::isfinite(guide.toDouble())) {
            result.error = QStringLiteral("%1 isn't a valid alignment.").arg(path);
            return result;
        }
        alignment.guides.push_back(guide.toDouble());
    }
    const QJsonObject photos = root.value(QStringLiteral("photos")).toObject();
    for (auto entry = photos.begin(); entry != photos.end(); ++entry) {
        const QJsonObject values = entry.value().toObject();
        core::PhotoAdjustment adjustment;
        adjustment.rollDegrees = values.value(QStringLiteral("roll_degrees")).toDouble(0.0);
        adjustment.pitchDegrees = values.value(QStringLiteral("pitch_degrees")).toDouble(0.0);
        adjustment.shift = values.value(QStringLiteral("shift")).toDouble(0.0);
        adjustment.scale = values.value(QStringLiteral("scale")).toDouble(1.0);
        constexpr double kMaxRotation = 45.0;
        if (!std::isfinite(adjustment.rollDegrees) || !std::isfinite(adjustment.pitchDegrees) ||
            !std::isfinite(adjustment.shift) || !std::isfinite(adjustment.scale) ||
            adjustment.scale <= 0.0 || std::abs(adjustment.rollDegrees) > kMaxRotation ||
            std::abs(adjustment.pitchDegrees) > kMaxRotation || entry.key().contains(u'/')) {
            result.error = QStringLiteral("%1 isn't a valid alignment.").arg(path);
            return result;
        }
        alignment.photos[entry.key().toStdString()] = adjustment;
    }
    result.alignment = alignment;
    return result;
}

bool writeAlignment(const Alignment& alignment, const QString& path) {
    QJsonArray guides;
    for (const double guide : alignment.guides) {
        guides.append(guide);
    }
    QJsonObject photos;
    for (const auto& [photo, adjustment] : alignment.photos) {
        if (!adjustment.isNeutral()) {
            photos.insert(QString::fromStdString(photo),
                          QJsonObject{{QStringLiteral("roll_degrees"), adjustment.rollDegrees},
                                      {QStringLiteral("pitch_degrees"), adjustment.pitchDegrees},
                                      {QStringLiteral("shift"), adjustment.shift},
                                      {QStringLiteral("scale"), adjustment.scale}});
        }
    }
    const QJsonObject root{{QStringLiteral("format_version"), kFormatVersion},
                           {QStringLiteral("guides"), guides},
                           {QStringLiteral("photos"), photos}};
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson());
    return file.commit();
}

}  // namespace streetpano::stitch
