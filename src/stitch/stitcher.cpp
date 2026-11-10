#include "stitch/stitcher.hpp"

#include <QImageReader>
#include <QSize>
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/seam_match.hpp"
#include "stitch/alignment.hpp"

namespace streetpano::stitch {
namespace {

[[nodiscard]] QString photoPath(const QDir& folder, const core::CameraPose& pose) {
    return folder.filePath(QString::fromStdString(pose.name));
}

constexpr int kMatchingPhotoWidth = 1200;  // Enough to judge alignment, and quick to decode.

[[nodiscard]] QImage readAtSize(const QDir& folder, const core::CameraPose& pose, QSize size) {
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
        QImage current = readAtSize(folder, poses[plan.strips[index].camera], size);
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
                            const std::atomic<bool>* cancel, const Alignment* alignment) {
    StitchResult result;

    PreparedStreet street = prepareStreet(poses, photoFolder, options, alignment);
    result.warnings = street.warnings;
    if (!street.error.isEmpty()) {
        result.error = street.error;
        return result;
    }
    std::vector<core::CameraPose>& usable = street.poses;
    const QSize photoSize = street.photoSize;
    core::FacadeSurface surface = street.surface;
    core::PlanResult planResult;
    planResult.plan = street.plan;
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
    // Keeps its alpha, because brightness matching and patches need to know what's empty.
    result.base = std::move(canvas);
    result.panorama = result.base;
    result.surface = std::move(surface);
    result.plan = plan;
    result.poses = std::move(usable);
    result.focalPixels = poses.focalPixels;
    result.photoSize = photoSize;
    return result;
}

QImage withPatches(const StitchResult& stitched, const QDir& photoFolder,
                   std::span<const core::Patch> patches, QStringList& warnings, int featherPixels) {
    if (patches.empty()) {
        return stitched.base;
    }
    QImage canvas = stitched.base.copy();
    const core::CanvasView view{reinterpret_cast<std::uint32_t*>(canvas.bits()), canvas.width(),
                                canvas.height(), static_cast<std::size_t>(canvas.bytesPerLine())};
    const QSize size(
        std::max(2, static_cast<int>(std::ceil(stitched.photoSize.width() * stitched.scale))),
        std::max(2, static_cast<int>(std::ceil(stitched.photoSize.height() * stitched.scale))));
    for (const core::Patch& patch : patches) {
        if (patch.photo.empty() || patch.hidden) {
            continue;
        }
        const auto pose = std::find_if(
            stitched.poses.begin(), stitched.poses.end(),
            [&patch](const core::CameraPose& item) { return item.name == patch.photo; });
        if (pose == stitched.poses.end()) {
            warnings.append(QStringLiteral("Skipped an area: %1 isn't part of this panorama.")
                                .arg(QString::fromStdString(patch.photo)));
            continue;
        }
        QImage photo = readAtSize(photoFolder, *pose, size);
        if (photo.isNull()) {
            warnings.append(QStringLiteral("Skipped an area: can't read %1.")
                                .arg(QString::fromStdString(patch.photo)));
            continue;
        }
        const double focal =
            stitched.focalPixels * size.width() / static_cast<double>(stitched.photoSize.width());
        core::drawPatch(stitched.surface, stitched.plan, patch.outline, *pose, focal, viewOf(photo),
                        view, featherPixels);
    }
    return canvas;
}

std::optional<std::string> cleanestPhoto(const StitchResult& stitched, const QDir& photoFolder,
                                         std::span<const core::WallPoint> outline) {
    constexpr std::size_t kMaxPhotos = 8;
    constexpr double kComparedPixels = 160.0;
    std::vector<std::size_t> choices =
        core::patchChoices(stitched.surface, stitched.poses, outline, stitched.focalPixels,
                           stitched.photoSize.width(), stitched.photoSize.height());
    if (choices.size() > kMaxPhotos) {
        choices.resize(kMaxPhotos);
    }
    double left = outline.front().arc;
    double right = left;
    double top = outline.front().height;
    double bottom = top;
    for (const core::WallPoint& point : outline) {
        left = std::min(left, point.arc);
        right = std::max(right, point.arc);
        top = std::max(top, point.height);
        bottom = std::min(bottom, point.height);
    }
    // A small, coarse version of the area is enough to compare photos.
    core::StitchPlan coarse = stitched.plan;
    coarse.unitsPerPixel = std::max(right - left, top - bottom) / kComparedPixels;
    if (coarse.unitsPerPixel <= 0.0) {
        return std::nullopt;
    }
    coarse.leftArc = left;
    coarse.topHeight = top;
    const int width =
        std::max(1, static_cast<int>(std::ceil((right - left) / coarse.unitsPerPixel)));
    const int height =
        std::max(1, static_cast<int>(std::ceil((top - bottom) / coarse.unitsPerPixel)));
    const double shrink =
        std::min(1.0, kMatchingPhotoWidth / static_cast<double>(stitched.photoSize.width()));
    const QSize size(std::max(2, static_cast<int>(stitched.photoSize.width() * shrink)),
                     std::max(2, static_cast<int>(stitched.photoSize.height() * shrink)));
    const double focal =
        stitched.focalPixels * size.width() / static_cast<double>(stitched.photoSize.width());

    std::vector<std::vector<std::uint32_t>> pixels;
    std::vector<std::size_t> cameras;
    pixels.reserve(choices.size());
    for (const std::size_t camera : choices) {
        const QImage photo = readAtSize(photoFolder, stitched.poses[camera], size);
        if (!photo.isNull()) {
            pixels.push_back(core::projectPhoto(stitched.surface, coarse, stitched.poses[camera],
                                                focal, viewOf(photo), 0, width, 0, height));
            cameras.push_back(camera);
        }
    }
    std::vector<core::ImageView> regions;
    regions.reserve(pixels.size());
    for (const std::vector<std::uint32_t>& region : pixels) {
        regions.push_back(core::ImageView{region.data(), width, height,
                                          static_cast<std::size_t>(width) * sizeof(std::uint32_t)});
    }
    const std::optional<std::size_t> best = core::cleanestRegion(regions);
    if (!best.has_value()) {
        return std::nullopt;
    }
    return stitched.poses[cameras[*best]].name;
}

}  // namespace streetpano::stitch
