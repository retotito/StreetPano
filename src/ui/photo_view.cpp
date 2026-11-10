#include "ui/photo_view.hpp"

#include <QMouseEvent>
#include <QNativeGestureEvent>
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
    const QRectF shown(offset_, QSizeF(image_.size()) * zoom_);
    if (gridSpacing_ > 0.0) {
        painter.setPen(QPen(QColor(255, 255, 255, 45), 1.0));
        // Counting lines, rather than adding up spacing, keeps rounding from creeping in.
        for (int line = 0; line * gridSpacing_ < image_.height(); ++line) {
            const double y = offset_.y() + line * gridSpacing_ * zoom_;
            painter.drawLine(QPointF(shown.left(), y), QPointF(shown.right(), y));
        }
        for (int line = 0; line * gridSpacing_ < image_.width(); ++line) {
            const double x = offset_.x() + line * gridSpacing_ * zoom_;
            painter.drawLine(QPointF(x, shown.top()), QPointF(x, shown.bottom()));
        }
    }
    for (std::size_t index = 0; index < guides_.size(); ++index) {
        const bool dragged = static_cast<int>(index) == draggedGuide_;
        painter.setPen(QPen(dragged ? QColor(255, 196, 0) : QColor(0, 200, 255), 2.0));
        const double y = offset_.y() + guides_[index] * zoom_;
        painter.drawLine(QPointF(shown.left(), y), QPointF(shown.right(), y));
    }
    painter.setRenderHint(QPainter::Antialiasing);
    for (std::size_t index = 0; index < outlines_.size(); ++index) {
        const bool selected = static_cast<int>(index) == selectedOutline_;
        drawOutline(outlines_[index], selected ? QColor(255, 196, 0) : QColor(90, 200, 250), true);
    }
    drawOutline(lasso_, QColor(255, 196, 0), markShape_ == MarkShape::Rectangle);
}

void PhotoView::resizeEvent(QResizeEvent* /*event*/) {
    if (fitted_) {
        fitToWindow();
    }
}

void PhotoView::wheelEvent(QWheelEvent* event) {
    // A trackpad reports exact pixels and moves the view, as elsewhere on a Mac. A mouse wheel
    // reports notches and zooms, which is quicker along a long panorama.
    if (!event->pixelDelta().isNull() && !image_.isNull()) {
        offset_ += QPointF(event->pixelDelta());
        fitted_ = false;
        update();
        event->accept();
        return;
    }
    constexpr double kNotchesPerDegree = 1.0 / 120.0;
    const double notches = event->angleDelta().y() * kNotchesPerDegree;
    zoomAround(event->position(), std::pow(1.25, notches));
    event->accept();
}

bool PhotoView::event(QEvent* event) {
    if (event->type() == QEvent::NativeGesture) {
        const auto* gesture = static_cast<QNativeGestureEvent*>(event);
        if (gesture->gestureType() == Qt::ZoomNativeGesture) {
            zoomAround(gesture->position(), 1.0 + gesture->value());
            return true;
        }
    }
    return QWidget::event(event);
}

QPointF PhotoView::toImage(QPointF widgetPoint) const { return (widgetPoint - offset_) / zoom_; }

void PhotoView::setLassoMode(bool enabled) {
    lassoMode_ = enabled;
    lasso_.clear();
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void PhotoView::fitHeight() {
    if (image_.isNull()) {
        return;
    }
    zoom_ = std::clamp(static_cast<double>(height()) / image_.height(),
                       std::min(fitZoom(), kMaxZoom), kMaxZoom);
    offset_ = QPointF(0.0, (height() - image_.height() * zoom_) / 2.0);
    fitted_ = false;
    update();
}

void PhotoView::showArea(const QRectF& imageArea) {
    constexpr double kShare = 0.6;  // Leaves some of the surroundings in view.
    if (image_.isNull() || imageArea.isEmpty()) {
        return;
    }
    const double fit =
        std::min(width() * kShare / imageArea.width(), height() * kShare / imageArea.height());
    zoom_ = std::clamp(fit, std::min(fitZoom(), kMaxZoom), kMaxZoom);
    offset_ = QPointF(width() / 2.0, height() / 2.0) - imageArea.center() * zoom_;
    fitted_ = false;
    update();
}

void PhotoView::setGrid(double spacing) {
    gridSpacing_ = spacing;
    update();
}

void PhotoView::setGuides(std::vector<double> rows) {
    guides_ = std::move(rows);
    draggedGuide_ = -1;
    update();
}

int PhotoView::guideAt(QPointF imagePoint) const {
    constexpr double kReachScreenPixels = 6.0;
    int found = -1;
    double nearest = kReachScreenPixels / std::max(zoom_, 1e-9);
    for (std::size_t index = 0; index < guides_.size(); ++index) {
        const double distance = std::abs(guides_[index] - imagePoint.y());
        if (distance <= nearest) {
            nearest = distance;
            found = static_cast<int>(index);
        }
    }
    return found;
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
    if (!lassoMode_ && event->button() == Qt::LeftButton && !image_.isNull()) {
        draggedGuide_ = guideAt(toImage(event->position()));
        if (draggedGuide_ >= 0) {
            update();
            return;
        }
    }
    if (lassoMode_ && event->button() == Qt::LeftButton && !image_.isNull()) {
        lassoStart_ = toImage(event->position());
        lasso_ = QPolygonF{lassoStart_};
        update();
    }
}

void PhotoView::mouseMoveEvent(QMouseEvent* event) {
    constexpr double kClickSlopPixels = 4.0;
    if ((event->position() - pressPoint_).manhattanLength() > kClickSlopPixels) {
        moved_ = true;
    }
    if (draggedGuide_ >= 0 && (event->buttons() & Qt::LeftButton) != 0U) {
        guides_[static_cast<std::size_t>(draggedGuide_)] = toImage(event->position()).y();
        update();
        return;
    }
    if (lassoMode_ && (event->buttons() & Qt::LeftButton) != 0U) {
        if (!lasso_.isEmpty()) {
            const QPointF point = toImage(event->position());
            switch (markShape_) {
                case MarkShape::Freehand:
                    lasso_.append(point);
                    break;
                case MarkShape::Rectangle:
                    lasso_ = QPolygonF{lassoStart_, QPointF(point.x(), lassoStart_.y()), point,
                                       QPointF(lassoStart_.x(), point.y())};
                    break;
                case MarkShape::Line:
                    lasso_ = QPolygonF{lassoStart_, point};
                    break;
            }
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
    if (event->button() == Qt::RightButton && !moved_ && !image_.isNull()) {
        emit imageRightClicked(toImage(event->position()), event->globalPosition().toPoint());
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }
    if (draggedGuide_ >= 0) {
        const int index = draggedGuide_;
        draggedGuide_ = -1;
        update();
        emit guideMoved(index, toImage(event->position()).y());
        return;
    }
    if (lassoMode_ && !lasso_.isEmpty()) {
        const QPolygonF finished = lasso_;
        lasso_.clear();
        update();
        // A rectangle or line needs a real drag. A freehand outline needs enough points.
        const bool usable = markShape_ == MarkShape::Freehand ? finished.size() >= kMinLassoPoints
                                                              : moved_ && finished.size() >= 2;
        if (usable) {
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
