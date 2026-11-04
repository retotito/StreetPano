#include "ui/film_strip_delegate.hpp"

#include <QImage>
#include <QPainter>
#include <QRect>

#include "ui/photo_list_model.hpp"

namespace streetpano::ui {
namespace {

const QColor kBackground(32, 34, 37);
const QColor kPlaceholder(58, 61, 66);
const QColor kSelected(37, 99, 235);
const QColor kCaption(200, 204, 208);
const QColor kError(248, 113, 113);

}  // namespace

FilmStripDelegate::FilmStripDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

QSize FilmStripDelegate::sizeHint(const QStyleOptionViewItem& /*option*/,
                                  const QModelIndex& /*index*/) const {
    return {kThumbnailWidth + 2 * kPadding, kThumbnailHeight + kCaptionHeight + 2 * kPadding};
}

void FilmStripDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const {
    const bool selected = (option.state & QStyle::State_Selected) != 0U;
    const bool hasError = index.data(PhotoListModel::kHasErrorRole).toBool();
    const QRect frame(option.rect.left() + kPadding, option.rect.top() + kPadding, kThumbnailWidth,
                      kThumbnailHeight);

    painter->save();
    painter->fillRect(option.rect, selected ? kSelected : kBackground);
    painter->fillRect(frame, kPlaceholder);

    const auto thumbnail = index.data(Qt::DecorationRole).value<QImage>();
    if (!thumbnail.isNull()) {
        const QSize fitted = thumbnail.size().scaled(frame.size(), Qt::KeepAspectRatio);
        QRect target(QPoint(0, 0), fitted);
        target.moveCenter(frame.center());
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        painter->drawImage(target, thumbnail);
    } else if (hasError) {
        painter->setPen(kError);
        painter->drawText(frame, Qt::AlignCenter, tr("Can't read"));
    }

    const QRect captionArea(frame.left(), frame.bottom() + 1, frame.width(), kCaptionHeight);
    painter->setPen(hasError ? kError : (selected ? QColor(Qt::white) : kCaption));
    painter->drawText(captionArea, Qt::AlignCenter,
                      painter->fontMetrics().elidedText(index.data(Qt::DisplayRole).toString(),
                                                        Qt::ElideMiddle, frame.width()));
    painter->restore();
}

}  // namespace streetpano::ui
