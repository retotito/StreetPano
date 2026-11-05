#include "ui/photo_view.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QRectF>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace streetpano::ui {

PhotoView::PhotoView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 180);
    setAutoFillBackground(false);
}

void PhotoView::setImage(const QImage& image) {
    image_ = image;
    message_.clear();
    if (fitted_) {
        fitToWindow();
    } else {
        update();
    }
}

void PhotoView::setMessage(const QString& message) {
    message_ = message;
    image_ = QImage{};
    update();
}

double PhotoView::fitZoom() const {
    if (image_.isNull()) {
        return 1.0;
    }
    return std::min(static_cast<double>(width()) / image_.width(),
                    static_cast<double>(height()) / image_.height());
}

void PhotoView::fitToWindow() {
    fitted_ = true;
    zoom_ = fitZoom();
    if (!image_.isNull()) {
        offset_ = QPointF((width() - image_.width() * zoom_) / 2.0,
                          (height() - image_.height() * zoom_) / 2.0);
    }
    update();
}

void PhotoView::zoomAround(QPointF widgetPoint, double factor) {
    if (image_.isNull() || factor <= 0.0) {
        return;
    }
    // Never smaller than the fitted view, because there's nothing useful to see there.
    const double minZoom = std::min(fitZoom(), kMaxZoom);
    const double newZoom = std::clamp(zoom_ * factor, minZoom, kMaxZoom);
    const QPointF imagePoint = (widgetPoint - offset_) / zoom_;
    zoom_ = newZoom;
    offset_ = widgetPoint - imagePoint * zoom_;
    fitted_ = false;
    if (zoom_ <= minZoom) {
        fitToWindow();
        return;
    }
    update();
}

void PhotoView::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(32, 34, 37));
    if (image_.isNull()) {
        painter.setPen(QColor(200, 204, 208));
        painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, message_);
        return;
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform, zoom_ < 2.0);
    painter.drawImage(QRectF(offset_, QSizeF(image_.size()) * zoom_), image_);

    const auto drawOutline = [this, &painter](const QPolygonF& outline, const QColor& colour,
                                              bool closed) {
        QPolygonF inWidget;
        inWidget.reserve(outline.size());
        for (const QPointF& point : outline) {
            inWidget.append(point * zoom_ + offset_);
        }
        painter.setPen(QPen(colour, 2.0));
        painter.setBrush(Qt::NoBrush);
        if (closed) {
            painter.drawPolygon(inWidget);
        } else {
            painter.drawPolyline(inWidget);
        }
    };
    painter.setRenderHint(QPainter::Antialiasing);
    for (std::size_t index = 0; index < outlines_.size(); ++index) {
        const bool selected = static_cast<int>(index) == selectedOutline_;
        drawOutline(outlines_[index], selected ? QColor(255, 196, 0) : QColor(90, 200, 250), true);
    }
    drawOutline(lasso_, QColor(255, 196, 0), false);
}

void PhotoView::resizeEvent(QResizeEvent* /*event*/) {
    if (fitted_) {
        fitToWindow();
    }
}

void PhotoView::wheelEvent(QWheelEvent* event) {
    constexpr double kNotchesPerDegree = 1.0 / 120.0;
    const double notches = event->angleDelta().y() * kNotchesPerDegree;
    zoomAround(event->position(), std::pow(1.25, notches));
    event->accept();
}

QPointF PhotoView::toImage(QPointF widgetPoint) const { return (widgetPoint - offset_) / zoom_; }

void PhotoView::setLassoMode(bool enabled) {
    lassoMode_ = enabled;
    lasso_.clear();
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void PhotoView::setOutlines(std::vector<QPolygonF> outlines, int selected) {
    outlines_ = std::move(outlines);
    selectedOutline_ = selected;
    update();
}

void PhotoView::mousePressEvent(QMouseEvent* event) {
    lastDragPoint_ = event->position();
    pressPoint_ = event->position();
    moved_ = false;
    if (lassoMode_ && event->button() == Qt::LeftButton && !image_.isNull()) {
        lasso_ = QPolygonF{toImage(event->position())};
        update();
    }
}

void PhotoView::mouseMoveEvent(QMouseEvent* event) {
    constexpr double kClickSlopPixels = 4.0;
    if ((event->position() - pressPoint_).manhattanLength() > kClickSlopPixels) {
        moved_ = true;
    }
    if (lassoMode_ && (event->buttons() & Qt::LeftButton) != 0U) {
        if (!lasso_.isEmpty()) {
            lasso_.append(toImage(event->position()));
            update();
        }
        return;
    }
    if ((event->buttons() & (Qt::LeftButton | Qt::RightButton)) != 0U) {
        offset_ += event->position() - lastDragPoint_;
        lastDragPoint_ = event->position();
        fitted_ = false;
        update();
    }
}

void PhotoView::mouseReleaseEvent(QMouseEvent* event) {
    constexpr qsizetype kMinLassoPoints = 3;
    if (event->button() != Qt::LeftButton) {
        return;
    }
    if (lassoMode_ && !lasso_.isEmpty()) {
        const QPolygonF finished = lasso_;
        lasso_.clear();
        update();
        if (finished.size() >= kMinLassoPoints) {
            emit lassoFinished(finished);
        }
        return;
    }
    if (!moved_ && !image_.isNull()) {
        emit imageClicked(toImage(event->position()));
    }
}

void PhotoView::mouseDoubleClickEvent(QMouseEvent* /*event*/) { fitToWindow(); }

}  // namespace streetpano::ui
