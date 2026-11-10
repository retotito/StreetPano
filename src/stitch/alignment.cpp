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
const QRgb kGapColour = qRgb(32, 34, 37);

[[nodiscard]] QString photoPath(const QDir& folder, const core::CameraPose& pose) {
    return folder.filePath(QString::fromStdString(pose.name));
}

}  // namespace

PreparedStreet prepareStreet(const PoseFile& poses, const QDir& photoFolder,
                             const core::StitchOptions& options) {
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
    view.unitsPerPixel = small.unitsPerPixel;
    view.topHeight = small.topHeight;

    struct Placed {
        std::size_t camera;
        core::PixelBox box;
    };
    std::vector<Placed> placed;
    int width = 0;
    for (std::size_t index = 0; index < street.poses.size(); ++index) {
        const auto box =
            core::photoBox(street.surface, small, street.poses[index], street.focalPixels,
                           street.photoSize.width(), street.photoSize.height());
        if (!box.has_value()) {
            view.warnings.append(QStringLiteral("%1 doesn't look at the facade.")
                                     .arg(QString::fromStdString(street.poses[index].name)));
            continue;
        }
        placed.push_back(Placed{index, *box});
        width += (box->right - box->left) + kGapPixels;
    }
    if (placed.empty()) {
        view.error = QStringLiteral("No photo looks at the facade.");
        return view;
    }
    view.strip = QImage(width, stripHeight, QImage::Format_ARGB32);
    if (view.strip.isNull()) {
        view.error = QStringLiteral("There isn't enough memory for the side-by-side view.");
        return view;
    }
    view.strip.fill(kGapColour);

    // Photos are read at about the size they're shown.
    const double fullUnitsPerPixel = street.surface.medianCameraDistance / street.focalPixels;
    const double shrink = std::min(1.0, kSharpness * fullUnitsPerPixel / small.unitsPerPixel);
    const QSize readSize(std::max(2, static_cast<int>(street.photoSize.width() * shrink)),
                         std::max(2, static_cast<int>(street.photoSize.height() * shrink)));
    const double focal =
        street.focalPixels * readSize.width() / static_cast<double>(street.photoSize.width());

    int left = 0;
    int done = 0;
    QPainter painter(&view.strip);
    for (const Placed& item : placed) {
        if (cancel != nullptr && cancel->load()) {
            view.error = QStringLiteral("Cancelled.");
            return view;
        }
        const core::CameraPose& pose = street.poses[item.camera];
        const int tileWidth = item.box.right - item.box.left;
        QImageReader reader(photoPath(photoFolder, pose));
        reader.setAutoTransform(false);
        reader.setScaledSize(readSize);
        QImage photo = reader.read();
        if (!photo.isNull()) {
            photo.convertTo(QImage::Format_ARGB32);
            const QImage& source = photo;
            const core::ImageView sourceView{
                reinterpret_cast<const std::uint32_t*>(source.constBits()), source.width(),
                source.height(), static_cast<std::size_t>(source.bytesPerLine())};
            const std::vector<std::uint32_t> pixels =
                core::projectPhoto(street.surface, small, pose, focal, sourceView, item.box.left,
                                   item.box.right, 0, stripHeight);
            const QImage tile(reinterpret_cast<const uchar*>(pixels.data()), tileWidth, stripHeight,
                              static_cast<qsizetype>(tileWidth) * 4, QImage::Format_ARGB32);
            painter.drawImage(left, 0, tile);
        } else {
            view.warnings.append(QStringLiteral("Skipped %1: %2")
                                     .arg(QString::fromStdString(pose.name), reader.errorString()));
        }
        view.tiles.push_back(AlignmentTile{pose.name, left, tileWidth});
        left += tileWidth + kGapPixels;
        ++done;
        if (progress) {
            progress(done, static_cast<int>(placed.size()));
        }
    }
    return view;
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
    for (const QJsonValue& guide : root.value(QStringLiteral("guides")).toArray()) {
        if (!guide.isDouble() || !std::isfinite(guide.toDouble())) {
            result.error = QStringLiteral("%1 isn't a valid alignment.").arg(path);
            return result;
        }
        alignment.guides.push_back(guide.toDouble());
    }
    result.alignment = alignment;
    return result;
}

bool writeAlignment(const Alignment& alignment, const QString& path) {
    QJsonArray guides;
    for (const double guide : alignment.guides) {
        guides.append(guide);
    }
    const QJsonObject root{{QStringLiteral("format_version"), kFormatVersion},
                           {QStringLiteral("guides"), guides}};
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson());
    return file.commit();
}

}  // namespace streetpano::stitch
