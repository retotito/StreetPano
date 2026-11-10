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
    // Built as locals and handed over under the lock, as with StitchJob.
    stitch::AlignmentView view;
    stitch::PreparedStreet street;
    stitch::Alignment alignment;
    const stitch::PoseFileResult poses = stitch::readPoseFile(posesPath_);
    if (poses.file.has_value()) {
        const stitch::AlignmentResult saved =
            stitch::readAlignment(stitch::alignmentPathFor(posesPath_));
        alignment = saved.alignment.value_or(stitch::Alignment{});
        street = stitch::prepareStreet(*poses.file, photoFolder_, {}, &alignment);
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
        street_ = std::move(street);
        alignment_ = std::move(alignment);
    }
    emit workerFinished(QPrivateSignal{});
}

stitch::PreparedStreet AlignJob::takeStreet() {
    const std::scoped_lock lock(mutex_);
    return std::exchange(street_, stitch::PreparedStreet{});
}

stitch::Alignment AlignJob::takeAlignment() {
    const std::scoped_lock lock(mutex_);
    return std::exchange(alignment_, stitch::Alignment{});
}

stitch::AlignmentView AlignJob::takeView() {
    const std::scoped_lock lock(mutex_);
    return std::exchange(view_, stitch::AlignmentView{});
}

}  // namespace streetpano::ui
