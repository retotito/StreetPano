#pragma once

#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace streetpano::ui {

struct LoadResult {
    QImage image;
    QString error;
};

// Reads an image, reduced to fit maxSide pixels if maxSide is above zero.
// This is safe to call on any thread, and it reports damaged files as errors.
[[nodiscard]] LoadResult loadImage(const QString& path, int maxSide);

// Loads images on background threads and delivers results on the owner's thread.
//
// Threads share only the two queues below, guarded by one standard mutex, which lets
// ThreadSanitizer verify every handoff. The destructor joins the workers, so a result
// can never arrive at an object that no longer exists.
class ImageLoader : public QObject {
    Q_OBJECT

  public:
    explicit ImageLoader(int threads, int maxSide, QObject* parent = nullptr);
    ~ImageLoader() override;

    ImageLoader(const ImageLoader&) = delete;
    ImageLoader& operator=(const ImageLoader&) = delete;
    ImageLoader(ImageLoader&&) = delete;
    ImageLoader& operator=(ImageLoader&&) = delete;

    // Does nothing if this key is already waiting or being loaded.
    void request(int key, const QString& path);

    // Drops waiting requests, for example when moving past them. A photo that's already
    // being read still arrives.
    void cancelPending();

  signals:
    void loaded(int key, const QImage& image);
    void failed(int key, const QString& message);

    // Emitted on a worker thread, and connected to drainResults on the owner's thread.
    void resultsReady();

  private:
    struct Request {
        int key;
        QString path;
    };
    struct Finished {
        int key;
        LoadResult result;
    };

    void runWorker();
    void drainResults();

    const int maxSide_;
    QSet<int> waiting_;  // Used only on the owner's thread.

    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Request> pending_;
    std::deque<Finished> finished_;
    bool stopping_ = false;

    std::vector<std::thread> workers_;
};

}  // namespace streetpano::ui
