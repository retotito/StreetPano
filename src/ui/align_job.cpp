#include "ui/align_job.hpp"

#include <utility>

namespace streetpano::ui {

AlignJob::AlignJob(QString posesPath, const QDir& photoFolder, int stripHeight, QObject* parent)
    : QObject(parent),
      posesPath_(std::move(posesPath)),
      photoFolder_(photoFolder),
      stripHeight_(stripHeight) {
    connect(this, &AlignJob::workerProgressed, this, &AlignJob::progressChanged,
            Qt::QueuedConnection);
    connect(this, &AlignJob::workerFinished, this, &AlignJob::finished, Qt::QueuedConnection);
}

AlignJob::~AlignJob() {
    cancel_.store(true);
    if (worker_.has_value()) {
        worker_->join();
    }
}

void AlignJob::start() {
    if (!worker_.has_value()) {
        worker_.emplace([this] { run(); });
    }
}

void AlignJob::run() {
    stitch::AlignmentView view;
    const stitch::PoseFileResult poses = stitch::readPoseFile(posesPath_);
    if (poses.file.has_value()) {
        const stitch::PreparedStreet street = stitch::prepareStreet(*poses.file, photoFolder_);
        view = stitch::buildAlignmentView(
            street, photoFolder_, stripHeight_,
            [this](int done, int total) {
                done_.store(done);
                total_.store(total);
                emit workerProgressed(QPrivateSignal{});
            },
            &cancel_);
        view.warnings = street.warnings + view.warnings;
    } else {
        view.error = poses.error;
    }
    {
        const std::scoped_lock lock(mutex_);
        view_ = std::move(view);
    }
    emit workerFinished(QPrivateSignal{});
}

stitch::AlignmentView AlignJob::takeView() {
    const std::scoped_lock lock(mutex_);
    return std::exchange(view_, stitch::AlignmentView{});
}

}  // namespace streetpano::ui
