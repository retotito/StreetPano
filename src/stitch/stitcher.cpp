#include "stitch/stitcher.hpp"

#include <QImageReader>
#include <QSize>
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <vector>

#include "core/seam_match.hpp"

namespace streetpano::stitch {
namespace {

[[nodiscard]] QString photoPath(const QDir& folder, const core::CameraPose& pose) {
    return folder.filePath(QString::fromStdString(pose.name));
}

constexpr int kMatchingPhotoWidth = 1200;  // Enough to judge alignment, and quick to decode.

[[nodiscard]] QImage readForMatching(const QDir& folder, const core::CameraPose& pose, QSize size) {
    QImageReader reader(photoPath(folder, pose));
    reader.setAutoTransform(false);
    reader.setScaledSize(size);
    QImage photo = reader.read();
    if (!photo.isNull()) {
        photo.convertTo(QImage::Format_ARGB32);
    }
    return photo;
}

[[nodiscard]] core::ImageView viewOf(const QImage& image) {
    return core::ImageView{reinterpret_cast<const std::uint32_t*>(image.constBits()), image.width(),
                           image.height(), static_cast<std::size_t>(image.bytesPerLine())};
}

// Moves the wall so that neighbouring photos agree at each seam, holding two photos at a time.
[[nodiscard]] core::FacadeSurface alignedSurface(const core::FacadeSurface& surface,
                                                 const core::StitchPlan& plan,
                                                 const std::vector<core::CameraPose>& poses,
                                                 const QDir& folder, double focal, QSize photoSize,
                                                 SeamAlignment& alignment,
                                                 const std::atomic<bool>* cancel) {
    const double shrink =
        std::min(1.0, kMatchingPhotoWidth / static_cast<double>(photoSize.width()));
    const QSize size(std::max(2, static_cast<int>(photoSize.width() * shrink)),
                     std::max(2, static_cast<int>(photoSize.height() * shrink)));
    const double matchingFocal = focal * size.width() / static_cast<double>(photoSize.width());

    std::vector<core::SeamOffset> seams;
    double scoreSum = 0.0;
    QImage previous;
    for (std::size_t index = 0; index < plan.strips.size(); ++index) {
        if (cancel != nullptr && cancel->load()) {
            return surface;
        }
        QImage current = readForMatching(folder, poses[plan.strips[index].camera], size);
        if (index > 0 && !previous.isNull() && !current.isNull()) {
            const double arc = plan.leftArc + plan.strips[index].xStart * plan.unitsPerPixel;
            const core::SeamMatch match = core::matchAtSeam(
                surface, arc, {&poses[plan.strips[index - 1].camera], viewOf(previous)},
                {&poses[plan.strips[index].camera], viewOf(current)}, matchingFocal);
            scoreSum += match.scoreAtWall;
            ++alignment.seams;
            if (match.offset.has_value()) {
                seams.push_back({arc, *match.offset});
                ++alignment.moved;
            }
        }
        previous = std::move(current);
    }
    if (alignment.seams > 0) {
        alignment.meanScoreBefore = scoreSum / alignment.seams;
    }
    if (seams.empty()) {
        return surface;
    }
    const std::vector<double> offsets = core::sampleOffsets(surface, seams);
    return core::withDepthOffsets(surface, offsets);
}

}  // namespace

StitchResult stitchPanorama(const PoseFile& poses, const QDir& photoFolder,
                            const core::StitchOptions& options, const StitchProgress& progress,
                            const std::atomic<bool>* cancel) {
    StitchResult result;

    // Unreadable photos are left out of the plan, so that their neighbours fill the gap.
    std::vector<core::CameraPose> usable;
    usable.reserve(poses.poses.size());
    QSize photoSize;
    for (const core::CameraPose& pose : poses.poses) {
        QImageReader reader(photoPath(photoFolder, pose));
        reader.setAutoTransform(false);
        const QSize size = reader.size();
        if (!size.isValid()) {
            result.warnings.append(
                QStringLiteral("Skipped %1: %2")
                    .arg(QString::fromStdString(pose.name), reader.errorString()));
            continue;
        }
        if (photoSize.isValid() && size != photoSize) {
            result.warnings.append(QStringLiteral("Skipped %1: it has a different size.")
                                       .arg(QString::fromStdString(pose.name)));
            continue;
        }
        photoSize = size;
        usable.push_back(pose);
    }
    if (usable.empty()) {
        result.error = QStringLiteral("None of the photos could be read.");
        return result;
    }

    const core::SurfaceResult surfaceResult = core::fitFacadeSurface(usable, poses.points);
    if (!surfaceResult.surface.has_value()) {
        result.error = QString::fromStdString(surfaceResult.error);
        return result;
    }
    core::FacadeSurface surface = *surfaceResult.surface;
    core::PlanResult planResult = core::makeStitchPlan(
        surface, usable, poses.focalPixels, photoSize.width(), photoSize.height(), options);
    if (!planResult.plan.has_value()) {
        result.error = QString::fromStdString(planResult.error);
        return result;
    }
    surface = alignedSurface(surface, *planResult.plan, usable, photoFolder, poses.focalPixels,
                             photoSize, result.alignment, cancel);
    planResult = core::makeStitchPlan(surface, usable, poses.focalPixels, photoSize.width(),
                                      photoSize.height(), options);
    if (!planResult.plan.has_value()) {
        result.error = QString::fromStdString(planResult.error);
        return result;
    }
    const core::StitchPlan& plan = *planResult.plan;
    result.scale = plan.scale;

    QImage canvas(plan.width, plan.height, QImage::Format_ARGB32);
    if (canvas.isNull()) {
        result.error = QStringLiteral("There isn't enough memory for a %1 x %2 panorama.")
                           .arg(plan.width)
                           .arg(plan.height);
        return result;
    }
    canvas.fill(0U);
    const core::CanvasView canvasView{reinterpret_cast<std::uint32_t*>(canvas.bits()),
                                      canvas.width(), canvas.height(),
                                      static_cast<std::size_t>(canvas.bytesPerLine())};

    // Photos don't need more detail than the panorama, so reduce them while reading.
    const QSize decodeSize(
        std::max(2, static_cast<int>(std::ceil(photoSize.width() * plan.scale))),
        std::max(2, static_cast<int>(std::ceil(photoSize.height() * plan.scale))));
    const double decodeFocal =
        poses.focalPixels * decodeSize.width() / static_cast<double>(photoSize.width());

    const int total = static_cast<int>(plan.strips.size());
    int done = 0;
    for (const core::Strip& strip : plan.strips) {
        if (cancel != nullptr && cancel->load()) {
            result.error = QStringLiteral("Stitching was cancelled.");
            return result;
        }
        const core::CameraPose& pose = usable[strip.camera];
        QImageReader reader(photoPath(photoFolder, pose));
        reader.setAutoTransform(false);
        if (decodeSize != photoSize) {
            reader.setScaledSize(decodeSize);
        }
        QImage photo = reader.read();
        if (photo.isNull()) {
            result.warnings.append(
                QStringLiteral("Skipped %1: %2")
                    .arg(QString::fromStdString(pose.name), reader.errorString()));
        } else {
            photo.convertTo(QImage::Format_ARGB32);
            const QImage& constPhoto = photo;
            const core::ImageView view{
                reinterpret_cast<const std::uint32_t*>(constPhoto.constBits()), constPhoto.width(),
                constPhoto.height(), static_cast<std::size_t>(constPhoto.bytesPerLine())};
            const double focal =
                decodeFocal * constPhoto.width() / static_cast<double>(decodeSize.width());
            core::drawStrip(surface, plan, strip, pose, focal, view, canvasView,
                            options.featherPixels);
        }
        ++done;
        if (progress) {
            progress(done, total);
        }
    }
    canvas.convertTo(QImage::Format_RGB32);
    result.panorama = std::move(canvas);
    return result;
}

}  // namespace streetpano::stitch
