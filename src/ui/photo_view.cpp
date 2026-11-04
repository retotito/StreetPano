#include "ui/photo_view.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QRectF>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

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

void PhotoView::mousePressEvent(QMouseEvent* event) { lastDragPoint_ = event->position(); }

void PhotoView::mouseMoveEvent(QMouseEvent* event) {
    if ((event->buttons() & Qt::LeftButton) != 0U) {
        offset_ += event->position() - lastDragPoint_;
        lastDragPoint_ = event->position();
        fitted_ = false;
        update();
    }
}

void PhotoView::mouseDoubleClickEvent(QMouseEvent* /*event*/) { fitToWindow(); }

}  // namespace streetpano::ui
