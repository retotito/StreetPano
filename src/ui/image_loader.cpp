#include "ui/image_loader.hpp"

#include <QImageReader>
#include <QSize>
#include <utility>

namespace streetpano::ui {

LoadResult loadImage(const QString& path, int maxSide) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    if (maxSide > 0) {
        const QSize size = reader.size();
        if (size.isValid() && (size.width() > maxSide || size.height() > maxSide)) {
            reader.setScaledSize(size.scaled(maxSide, maxSide, Qt::KeepAspectRatio));
        }
    }
    QImage image = reader.read();
    if (image.isNull()) {
        return LoadResult{QImage{}, reader.errorString()};
    }
    return LoadResult{std::move(image), QString{}};
}

ImageLoader::ImageLoader(int threads, int maxSide, QObject* parent)
    : QObject(parent), maxSide_(maxSide) {
    connect(this, &ImageLoader::resultsReady, this, &ImageLoader::drainResults,
            Qt::QueuedConnection);
    workers_.reserve(static_cast<std::size_t>(threads));
    for (int index = 0; index < threads; ++index) {
        workers_.emplace_back([this] { runWorker(); });
    }
}

ImageLoader::~ImageLoader() {
    {
        const std::scoped_lock lock(mutex_);
        stopping_ = true;
        pending_.clear();
    }
    wake_.notify_all();
    for (std::thread& worker : workers_) {
        worker.join();
    }
}

void ImageLoader::request(int key, const QString& path) {
    if (waiting_.contains(key)) {
        return;
    }
    waiting_.insert(key);
    {
        const std::scoped_lock lock(mutex_);
        pending_.push_back(Request{key, path});
    }
    wake_.notify_one();
}

void ImageLoader::cancelPending() {
    {
        const std::scoped_lock lock(mutex_);
        pending_.clear();
    }
    waiting_.clear();
}

void ImageLoader::runWorker() {
    while (true) {
        Request request;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
            if (stopping_) {
                return;
            }
            request = std::move(pending_.front());
            pending_.pop_front();
        }
        LoadResult result = loadImage(request.path, maxSide_);
        {
            const std::scoped_lock lock(mutex_);
            finished_.push_back(Finished{request.key, std::move(result)});
        }
        emit resultsReady();
    }
}

void ImageLoader::drainResults() {
    std::deque<Finished> batch;
    {
        const std::scoped_lock lock(mutex_);
        batch.swap(finished_);
    }
    for (const Finished& item : batch) {
        waiting_.remove(item.key);
        if (item.result.image.isNull()) {
            emit failed(item.key, item.result.error);
        } else {
            emit loaded(item.key, item.result.image);
        }
    }
}

}  // namespace streetpano::ui
