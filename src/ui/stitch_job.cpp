#include "ui/stitch_job.hpp"

#include <utility>

#include "stitch/pose_file.hpp"

namespace streetpano::ui {

StitchJob::StitchJob(QString posesPath, const QDir& photoFolder, QString outputPath,
                     QObject* parent)
    : QObject(parent),
      posesPath_(std::move(posesPath)),
      photoFolder_(photoFolder),
      outputPath_(std::move(outputPath)) {}

StitchJob::~StitchJob() {
    cancel_.store(true);
    if (worker_.has_value()) {
        worker_->join();
    }
}

void StitchJob::start() {
    if (!worker_.has_value()) {
        worker_.emplace([this] { run(); });
    }
}

void StitchJob::run() {
    stitch::StitchResult result;
    const stitch::PoseFileResult poses = stitch::readPoseFile(posesPath_);
    if (poses.file.has_value()) {
        result = stitch::stitchPanorama(
            *poses.file, photoFolder_, core::StitchOptions{},
            [this](int done, int total) {
                done_.store(done);
                total_.store(total);
                emit progressChanged();
            },
            &cancel_);
        if (!result.panorama.isNull() && !result.panorama.save(outputPath_, nullptr, 95)) {
            result.warnings.append(
                QStringLiteral("The panorama couldn't be saved to %1.").arg(outputPath_));
        }
    } else {
        result.error = poses.error;
    }
    {
        const std::scoped_lock lock(mutex_);
        result_ = std::move(result);
    }
    emit finished();
}

stitch::StitchResult StitchJob::takeResult() {
    const std::scoped_lock lock(mutex_);
    return std::exchange(result_, stitch::StitchResult{});
}

}  // namespace streetpano::ui
