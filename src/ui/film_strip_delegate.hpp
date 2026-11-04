#pragma once

#include <QStyledItemDelegate>

namespace streetpano::ui {

// Draws film strip items at a fixed size, whether or not the thumbnail has loaded yet.
class FilmStripDelegate : public QStyledItemDelegate {
    Q_OBJECT

  public:
    static constexpr int kThumbnailWidth = 192;
    static constexpr int kThumbnailHeight = 108;
    static constexpr int kCaptionHeight = 22;
    static constexpr int kPadding = 6;

    explicit FilmStripDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override;
};

}  // namespace streetpano::ui
