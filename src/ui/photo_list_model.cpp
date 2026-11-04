#include "ui/photo_list_model.hpp"

#include <QFileInfo>
#include <QVariant>

namespace streetpano::ui {

PhotoListModel::PhotoListModel(std::size_t budgetBytes, QObject* parent)
    : QAbstractListModel(parent), cache_(budgetBytes) {
    createLoader();
}

void PhotoListModel::createLoader() {
    // Replacing the loader waits for its workers and discards undelivered results, so
    // thumbnails from a previous folder can't appear in this one.
    loader_.reset();
    loader_ = std::make_unique<ImageLoader>(2, kThumbnailSide);
    connect(loader_.get(), &ImageLoader::loaded, this, &PhotoListModel::onLoaded);
    connect(loader_.get(), &ImageLoader::failed, this, &PhotoListModel::onFailed);
}

void PhotoListModel::setPhotos(const QStringList& paths) {
    beginResetModel();
    createLoader();
    paths_ = paths;
    cache_.clear();
    errors_.clear();
    requestedCount_ = 0;
    endResetModel();
    emit cacheChanged();
}

int PhotoListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(paths_.size());
}

QVariant PhotoListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= paths_.size()) {
        return {};
    }
    const int row = index.row();
    switch (role) {
        case Qt::DisplayRole:
            return QFileInfo(paths_.at(row)).fileName();
        case kHasErrorRole:
            return errors_.contains(row);
        case Qt::ToolTipRole:
            return errors_.contains(row) ? errors_.value(row) : paths_.at(row);
        case Qt::DecorationRole: {
            if (errors_.contains(row)) {
                return {};
            }
            std::optional<QImage> image = cache_.get(row);
            if (image.has_value()) {
                return *image;
            }
            ++requestedCount_;
            loader_->request(row, paths_.at(row));
            return {};
        }
        default:
            return {};
    }
}

QString PhotoListModel::pathAt(int row) const {
    return row >= 0 && row < paths_.size() ? paths_.at(row) : QString{};
}

void PhotoListModel::onLoaded(int row, const QImage& image) {
    cache_.put(row, image, static_cast<std::size_t>(image.sizeInBytes()));
    const QModelIndex changed = index(row);
    emit dataChanged(changed, changed, {Qt::DecorationRole});
    emit cacheChanged();
}

void PhotoListModel::onFailed(int row, const QString& message) {
    errors_.insert(row, message);
    const QModelIndex changed = index(row);
    emit dataChanged(changed, changed, {Qt::DecorationRole, Qt::ToolTipRole});
}

}  // namespace streetpano::ui
