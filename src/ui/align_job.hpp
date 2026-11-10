#pragma once

#include <QDir>
#include <QObject>
#include <QString>
#include <atomic>
#include <mutex>
#include <optional>
#include <thread>

#include "stitch/alignment.hpp"

namespace streetpano::ui {

// Builds the side-by-side view of a street on a background thread. Like StitchJob, the destructor
// stops and joins the worker, and results pass through a mutex-guarded member.
class AlignJob : public QObject {
    Q_OBJECT

  public:
    explicit AlignJob(QString posesPath, const QDir& photoFolder, int stripHeight,
                      QObject* parent = nullptr);
    ~AlignJob() override;

    AlignJob(const AlignJob&) = delete;
    AlignJob& operator=(const AlignJob&) = delete;
    AlignJob(AlignJob&&) = delete;
    AlignJob& operator=(AlignJob&&) = delete;

    void start();

    [[nodiscard]] int done() const { return done_.load(); }
    [[nodiscard]] int total() const { return total_.load(); }

    // Valid after finished has been emitted.
    [[nodiscard]] stitch::AlignmentView takeView();
    [[nodiscard]] stitch::PreparedStreet takeStreet();
    [[nodiscard]] stitch::Alignment takeAlignment();

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
    const int stripHeight_;

    std::atomic<bool> cancel_{false};
    std::atomic<int> done_{0};
    std::atomic<int> total_{0};

    std::mutex mutex_;
    stitch::AlignmentView view_;
    stitch::PreparedStreet street_;
    stitch::Alignment alignment_;

    std::optional<std::thread> worker_;
};

}  // namespace streetpano::ui
