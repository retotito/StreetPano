#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QImage>
#include <QString>
#include <QStringList>
#include <cstddef>
#include <memory>

#include "core/lru_cache.hpp"
#include "ui/image_loader.hpp"

namespace streetpano::ui {

// Provides the film strip's items. Thumbnails load only when a view asks for them, which
// means only for visible photos, and they stay within a fixed memory budget.
class PhotoListModel : public QAbstractListModel {
    Q_OBJECT

  public:
    static constexpr std::size_t kDefaultBudgetBytes = std::size_t{64} * 1024 * 1024;
    static constexpr int kThumbnailSide = 192;
    static constexpr int kHasErrorRole = Qt::UserRole + 1;

    explicit PhotoListModel(std::size_t budgetBytes = kDefaultBudgetBytes,
                            QObject* parent = nullptr);

    void setPhotos(const QStringList& paths);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;

    [[nodiscard]] QString pathAt(int row) const;
    [[nodiscard]] std::size_t cacheUsedBytes() const { return cache_.usedBytes(); }
    [[nodiscard]] std::size_t cacheBudgetBytes() const { return cache_.budgetBytes(); }
    [[nodiscard]] int requestedCount() const { return requestedCount_; }

  signals:
    void cacheChanged();

  private:
    void onLoaded(int row, const QImage& image);
    void onFailed(int row, const QString& message);
    void createLoader();

    QStringList paths_;
    // data() is const for Qt's interface, but asking for a thumbnail fills these caches.
    mutable core::LruCache<int, QImage> cache_;
    mutable int requestedCount_ = 0;
    QHash<int, QString> errors_;
    std::unique_ptr<ImageLoader> loader_;
};

}  // namespace streetpano::ui
