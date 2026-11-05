#pragma once

#include <QDir>
#include <QObject>
#include <QString>
#include <atomic>
#include <mutex>
#include <optional>
#include <thread>

#include "stitch/decisions.hpp"
#include "stitch/stitcher.hpp"

namespace streetpano::ui {

// Runs one stitch on a background thread. As with ImageLoader, the destructor stops and joins
// the worker, and results pass through a mutex-guarded member.
class StitchJob : public QObject {
    Q_OBJECT

  public:
    explicit StitchJob(QString posesPath, const QDir& photoFolder, QString outputPath,
                       QObject* parent = nullptr);
    ~StitchJob() override;

    StitchJob(const StitchJob&) = delete;
    StitchJob& operator=(const StitchJob&) = delete;
    StitchJob(StitchJob&&) = delete;
    StitchJob& operator=(StitchJob&&) = delete;

    void start();

    [[nodiscard]] int done() const { return done_.load(); }
    [[nodiscard]] int total() const { return total_.load(); }

    // Valid after finished has been emitted.
    [[nodiscard]] stitch::StitchResult takeResult();
    [[nodiscard]] stitch::Decisions takeDecisions();
    [[nodiscard]] QString decisionsPath() const { return stitch::decisionsPathFor(posesPath_); }
    [[nodiscard]] const QString& outputPath() const { return outputPath_; }

  signals:
    // Both are always emitted on the owner's thread, so they're safe to connect directly.
    void progressChanged();
    void finished();

    // Emitted on the worker thread. Only this class connects to these.
    void workerProgressed(QPrivateSignal);
    void workerFinished(QPrivateSignal);

  private:
    void run();

    const QString posesPath_;
    const QDir photoFolder_;
    const QString outputPath_;

    std::atomic<bool> cancel_{false};
    std::atomic<int> done_{0};
    std::atomic<int> total_{0};

    std::mutex mutex_;
    stitch::StitchResult result_;
    stitch::Decisions decisions_;

    std::optional<std::thread> worker_;
};

}  // namespace streetpano::ui
