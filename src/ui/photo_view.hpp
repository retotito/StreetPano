#pragma once

#include <QImage>
#include <QPointF>
#include <QString>
#include <QWidget>

namespace streetpano::ui {

// Shows one image, with zoom around the cursor and drag to pan.
class PhotoView : public QWidget {
    Q_OBJECT

  public:
    static constexpr double kMaxZoom = 16.0;

    explicit PhotoView(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void setMessage(const QString& message);
    void fitToWindow();
    void zoomAround(QPointF widgetPoint, double factor);

    [[nodiscard]] const QImage& image() const { return image_; }
    [[nodiscard]] const QString& message() const { return message_; }
    [[nodiscard]] double zoom() const { return zoom_; }
    [[nodiscard]] QPointF offset() const { return offset_; }
    [[nodiscard]] bool isFitted() const { return fitted_; }

  protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

  private:
    [[nodiscard]] double fitZoom() const;

    QImage image_;
    QString message_;
    double zoom_ = 1.0;
    QPointF offset_;
    QPointF lastDragPoint_;
    bool fitted_ = true;
};

}  // namespace streetpano::ui
