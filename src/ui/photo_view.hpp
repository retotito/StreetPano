#pragma once

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QWidget>
#include <cstddef>
#include <vector>

namespace streetpano::ui {

// Shows one image, with zoom around the cursor and drag to pan. In lasso mode, dragging with the
// left button draws an outline instead, and the right button still pans.
class PhotoView : public QWidget {
    Q_OBJECT

  public:
    static constexpr double kMaxZoom = 16.0;

    // What a drag draws in lasso mode. A line is for marking something that should be straight.
    enum class MarkShape { Freehand, Rectangle, Line };

    explicit PhotoView(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void setMessage(const QString& message);
    void fitToWindow();
    void zoomAround(QPointF widgetPoint, double factor);

    void setLassoMode(bool enabled);
    void setMarkShape(MarkShape shape) { markShape_ = shape; }
    [[nodiscard]] MarkShape markShape() const { return markShape_; }
    // Zooms and pans so that this part of the image fills most of the view.
    void showArea(const QRectF& imageArea);
    // Outlines to draw over the image, in image coordinates. selected is highlighted, or -1.
    void setOutlines(std::vector<QPolygonF> outlines, int selected);

    [[nodiscard]] bool isLassoMode() const { return lassoMode_; }
    [[nodiscard]] std::size_t outlineCount() const { return outlines_.size(); }
    [[nodiscard]] const QImage& image() const { return image_; }
    [[nodiscard]] const QString& message() const { return message_; }
    [[nodiscard]] double zoom() const { return zoom_; }
    [[nodiscard]] QPointF offset() const { return offset_; }
    [[nodiscard]] bool isFitted() const { return fitted_; }

  signals:
    // The finished outline, in image coordinates.
    void lassoFinished(const QPolygonF& outline);
    // A click that wasn't a drag, in image coordinates.
    void imageClicked(QPointF point);
    // A right-click that wasn't a drag, in image coordinates, and on the screen.
    void imageRightClicked(QPointF point, QPoint screenPoint);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool event(QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

  private:
    [[nodiscard]] double fitZoom() const;
    [[nodiscard]] QPointF toImage(QPointF widgetPoint) const;

    QImage image_;
    QString message_;
    double zoom_ = 1.0;
    QPointF offset_;
    QPointF lastDragPoint_;
    bool fitted_ = true;
    bool lassoMode_ = false;
    bool moved_ = false;
    QPointF pressPoint_;
    QPolygonF lasso_;
    QPointF lassoStart_;
    MarkShape markShape_ = MarkShape::Freehand;
    std::vector<QPolygonF> outlines_;
    int selectedOutline_ = -1;
};

}  // namespace streetpano::ui
