#include "ui/photo_view.hpp"

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPointingDevice>
#include <QPolygonF>
#include <QSignalSpy>
#include <QWheelEvent>
#include <QtTest>
#include <utility>
#include <vector>

using streetpano::ui::PhotoView;

class PhotoViewTest : public QObject {
    Q_OBJECT

  private slots:
    void lassoReportsAnOutlineInImageCoordinates();
    void aShortLassoIsIgnored();
    void rightButtonStillPansInLassoMode();
    void aClickReportsItsImagePoint();
    void aDragIsNotAClick();
    void aRightClickReportsItsImagePoint();
    void aTrackpadMovesTheView();
    void aRectangleIsDraggedFromCornerToCorner();
    void aLineIsDraggedFromStartToEnd();
    void aClickDrawsNoRectangleOrLine();
    void pinchingZooms();
    void outlinesAreDrawnOverTheImage();
    void startsEmptyWithAMessage();
    void fitsAnImageInsideTheWidget();
    void zoomKeepsThePointUnderTheCursorStill();
    void zoomStopsAtTheMaximum();
    void zoomingFarOutReturnsToFit();
    void ignoresZoomWithoutAnImage();
    void dragMovesTheImage();
    void doubleClickRestoresFit();
    void resizeKeepsAFittedImageFitted();
    void wheelZoomsIn();
    void paintsWithAndWithoutAnImage();
    void messageReplacesTheImage();
};

namespace {

QImage testImage() {
    QImage image(400, 200, QImage::Format_RGB32);
    image.fill(Qt::blue);
    return image;
}

}  // namespace

void PhotoViewTest::startsEmptyWithAMessage() {
    PhotoView view;
    view.setMessage(QStringLiteral("hello"));
    QCOMPARE(view.message(), QStringLiteral("hello"));
    QVERIFY(view.image().isNull());
}

void PhotoViewTest::fitsAnImageInsideTheWidget() {
    PhotoView view;
    view.resize(800, 800);
    view.setImage(testImage());
    QCOMPARE(view.zoom(), 2.0);
    QCOMPARE(view.offset(), QPointF(0.0, 200.0));
    QVERIFY(view.isFitted());
}

void PhotoViewTest::zoomKeepsThePointUnderTheCursorStill() {
    PhotoView view;
    view.resize(800, 800);
    view.setImage(testImage());
    const QPointF cursor(300.0, 300.0);
    const QPointF before = (cursor - view.offset()) / view.zoom();
    view.zoomAround(cursor, 1.5);
    const QPointF after = (cursor - view.offset()) / view.zoom();
    QVERIFY(qFuzzyCompare(before.x(), after.x()));
    QVERIFY(qFuzzyCompare(before.y(), after.y()));
    QVERIFY(!view.isFitted());
}

void PhotoViewTest::zoomStopsAtTheMaximum() {
    PhotoView view;
    view.resize(800, 800);
    view.setImage(testImage());
    view.zoomAround(QPointF(10.0, 10.0), 1000.0);
    QCOMPARE(view.zoom(), PhotoView::kMaxZoom);
}

void PhotoViewTest::zoomingFarOutReturnsToFit() {
    PhotoView view;
    view.resize(800, 800);
    view.setImage(testImage());
    view.zoomAround(QPointF(10.0, 10.0), 3.0);
    view.zoomAround(QPointF(10.0, 10.0), 0.001);
    QVERIFY(view.isFitted());
    QCOMPARE(view.zoom(), 2.0);
}

void PhotoViewTest::ignoresZoomWithoutAnImage() {
    PhotoView view;
    view.zoomAround(QPointF(1.0, 1.0), 2.0);
    QCOMPARE(view.zoom(), 1.0);
    view.setImage(testImage());
    const double zoom = view.zoom();
    view.zoomAround(QPointF(1.0, 1.0), 0.0);
    view.zoomAround(QPointF(1.0, 1.0), -1.0);
    QCOMPARE(view.zoom(), zoom);
}

void PhotoViewTest::dragMovesTheImage() {
    PhotoView view;
    view.resize(800, 800);
    view.setImage(testImage());
    const QPointF start = view.offset();
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
    QMouseEvent move(QEvent::MouseMove, QPointF(130.0, 120.0), QPointF(130.0, 120.0), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &move);
    QCOMPARE(view.offset(), start + QPointF(30.0, 20.0));
    QVERIFY(!view.isFitted());

    QMouseEvent hover(QEvent::MouseMove, QPointF(300.0, 300.0), QPointF(300.0, 300.0), Qt::NoButton,
                      Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &hover);
    QCOMPARE(view.offset(), start + QPointF(30.0, 20.0));
}

void PhotoViewTest::doubleClickRestoresFit() {
    PhotoView view;
    view.resize(800, 800);
    view.setImage(testImage());
    view.zoomAround(QPointF(10.0, 10.0), 3.0);
    QTest::mouseDClick(&view, Qt::LeftButton);
    QVERIFY(view.isFitted());
}

void PhotoViewTest::resizeKeepsAFittedImageFitted() {
    PhotoView view;
    view.resize(800, 800);
    view.setImage(testImage());
    view.show();
    view.resize(400, 400);
    QTRY_COMPARE(view.zoom(), 1.0);
    view.zoomAround(QPointF(10.0, 10.0), 2.0);
    view.resize(800, 800);
    QCOMPARE(view.zoom(), 2.0);
}

void PhotoViewTest::wheelZoomsIn() {
    PhotoView view;
    view.resize(800, 800);
    view.setImage(testImage());
    const double zoom = view.zoom();
    QWheelEvent wheel(QPointF(100.0, 100.0), QPointF(100.0, 100.0), QPoint(), QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&view, &wheel);
    QCOMPARE(view.zoom(), zoom * 1.25);
}

void PhotoViewTest::paintsWithAndWithoutAnImage() {
    PhotoView view;
    view.resize(400, 200);
    view.setMessage(QStringLiteral("no image"));
    QImage empty = view.grab().toImage();
    QVERIFY(!empty.isNull());

    view.setImage(testImage());
    const QImage painted = view.grab().toImage();
    QCOMPARE(painted.pixelColor(200, 100), QColor(Qt::blue));

    view.zoomAround(QPointF(200.0, 100.0), 4.0);
    QCOMPARE(view.grab().toImage().pixelColor(200, 100), QColor(Qt::blue));
}

void PhotoViewTest::messageReplacesTheImage() {
    PhotoView view;
    view.setImage(testImage());
    view.setMessage(QStringLiteral("failed"));
    QVERIFY(view.image().isNull());
    view.setImage(testImage());
    QVERIFY(view.message().isEmpty());
}

void PhotoViewTest::lassoReportsAnOutlineInImageCoordinates() {
    PhotoView view;
    view.resize(400, 200);
    QImage image(800, 400, QImage::Format_RGB32);
    image.fill(Qt::gray);
    view.setImage(image);  // Fitted at half size, so widget points are half of image points.
    view.setLassoMode(true);
    QSignalSpy finished(&view, &PhotoView::lassoFinished);
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, QPoint(10, 20));
    for (const QPoint point : {QPoint(50, 20), QPoint(50, 60), QPoint(10, 60)}) {
        QMouseEvent move(QEvent::MouseMove, QPointF(point), view.mapToGlobal(QPointF(point)),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&view, &move);
    }
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, QPoint(10, 60));
    QCOMPARE(finished.count(), 1);
    const auto outline = finished.at(0).at(0).value<QPolygonF>();
    QCOMPARE(outline.size(), 4);
    QCOMPARE(outline.at(0), QPointF(20.0, 40.0));
    QCOMPARE(outline.at(2), QPointF(100.0, 120.0));
    view.setLassoMode(false);
    QVERIFY(!view.isLassoMode());
}

void PhotoViewTest::aShortLassoIsIgnored() {
    PhotoView view;
    view.resize(400, 200);
    QImage image(400, 200, QImage::Format_RGB32);
    image.fill(Qt::gray);
    view.setImage(image);
    view.setLassoMode(true);
    QSignalSpy finished(&view, &PhotoView::lassoFinished);
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, QPoint(10, 20));
    QCOMPARE(finished.count(), 0);
}

void PhotoViewTest::rightButtonStillPansInLassoMode() {
    PhotoView view;
    view.resize(400, 200);
    QImage image(400, 200, QImage::Format_RGB32);
    image.fill(Qt::gray);
    view.setImage(image);
    view.setLassoMode(true);
    const QPointF before = view.offset();
    QTest::mousePress(&view, Qt::RightButton, Qt::NoModifier, QPoint(10, 20));
    QMouseEvent move(QEvent::MouseMove, QPointF(30, 20), view.mapToGlobal(QPointF(30, 20)),
                     Qt::NoButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &move);
    QTest::mouseRelease(&view, Qt::RightButton, Qt::NoModifier, QPoint(30, 20));
    QCOMPARE(view.offset().x(), before.x() + 20.0);
}

void PhotoViewTest::aClickReportsItsImagePoint() {
    PhotoView view;
    view.resize(400, 200);
    QImage image(800, 400, QImage::Format_RGB32);
    image.fill(Qt::gray);
    view.setImage(image);
    QSignalSpy clicked(&view, &PhotoView::imageClicked);
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, QPoint(10, 20));
    QCOMPARE(clicked.count(), 1);
    QCOMPARE(clicked.at(0).at(0).toPointF(), QPointF(20.0, 40.0));
}

void PhotoViewTest::aDragIsNotAClick() {
    PhotoView view;
    view.resize(400, 200);
    QImage image(400, 200, QImage::Format_RGB32);
    image.fill(Qt::gray);
    view.setImage(image);
    QSignalSpy clicked(&view, &PhotoView::imageClicked);
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, QPoint(10, 20));
    QMouseEvent move(QEvent::MouseMove, QPointF(60, 20), view.mapToGlobal(QPointF(60, 20)),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &move);
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, QPoint(60, 20));
    QCOMPARE(clicked.count(), 0);
}

void PhotoViewTest::aRightClickReportsItsImagePoint() {
    PhotoView view;
    view.resize(400, 200);
    QImage image(800, 400, QImage::Format_RGB32);
    image.fill(Qt::gray);
    view.setImage(image);
    QSignalSpy clicked(&view, &PhotoView::imageRightClicked);
    QTest::mouseClick(&view, Qt::RightButton, Qt::NoModifier, QPoint(10, 20));
    QCOMPARE(clicked.count(), 1);
    QCOMPARE(clicked.at(0).at(0).toPointF(), QPointF(20.0, 40.0));
}

namespace {

QPolygonF dragShape(PhotoView& view, PhotoView::MarkShape shape, QPoint from, QPoint to) {
    view.setMarkShape(shape);
    view.setLassoMode(true);
    QSignalSpy finished(&view, &PhotoView::lassoFinished);
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, from);
    QMouseEvent move(QEvent::MouseMove, QPointF(to), view.mapToGlobal(QPointF(to)), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &move);
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, to);
    return finished.isEmpty() ? QPolygonF{} : finished.at(0).at(0).value<QPolygonF>();
}

}  // namespace

void PhotoViewTest::aRectangleIsDraggedFromCornerToCorner() {
    PhotoView view;
    view.resize(400, 200);
    QImage image(800, 400, QImage::Format_RGB32);
    image.fill(Qt::gray);
    view.setImage(image);  // Fitted at half size.
    const QPolygonF rectangle =
        dragShape(view, PhotoView::MarkShape::Rectangle, QPoint(10, 20), QPoint(60, 80));
    QCOMPARE(rectangle.size(), 4);
    QCOMPARE(rectangle.boundingRect(), QRectF(20, 40, 100, 120));
    QVERIFY(view.markShape() == PhotoView::MarkShape::Rectangle);
}

void PhotoViewTest::aLineIsDraggedFromStartToEnd() {
    PhotoView view;
    view.resize(400, 200);
    QImage image(800, 400, QImage::Format_RGB32);
    image.fill(Qt::gray);
    view.setImage(image);
    const QPolygonF line =
        dragShape(view, PhotoView::MarkShape::Line, QPoint(10, 20), QPoint(110, 25));
    QCOMPARE(line.size(), 2);
    QCOMPARE(line.at(0), QPointF(20, 40));
    QCOMPARE(line.at(1), QPointF(220, 50));
}

void PhotoViewTest::aClickDrawsNoRectangleOrLine() {
    PhotoView view;
    view.resize(400, 200);
    QImage image(800, 400, QImage::Format_RGB32);
    image.fill(Qt::gray);
    view.setImage(image);
    QVERIFY(dragShape(view, PhotoView::MarkShape::Line, QPoint(10, 20), QPoint(11, 20)).isEmpty());
    QVERIFY(
        dragShape(view, PhotoView::MarkShape::Rectangle, QPoint(10, 20), QPoint(11, 21)).isEmpty());
}

void PhotoViewTest::aTrackpadMovesTheView() {
    PhotoView view;
    view.resize(400, 200);
    QImage image(1600, 400, QImage::Format_RGB32);
    image.fill(Qt::gray);
    view.setImage(image);
    view.zoomAround(QPointF(200, 100), 4.0);
    const double zoom = view.zoom();
    const QPointF before = view.offset();
    QWheelEvent scroll(QPointF(200, 100), view.mapToGlobal(QPointF(200, 100)), QPoint(-30, 10),
                       QPoint(-90, 30), Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
    QApplication::sendEvent(&view, &scroll);
    QCOMPARE(view.zoom(), zoom);
    QCOMPARE(view.offset(), before + QPointF(-30, 10));
}

void PhotoViewTest::pinchingZooms() {
    PhotoView view;
    view.resize(400, 200);
    QImage image(1600, 400, QImage::Format_RGB32);
    image.fill(Qt::gray);
    view.setImage(image);
    const double fitted = view.zoom();
    QNativeGestureEvent pinch(Qt::ZoomNativeGesture, QPointingDevice::primaryPointingDevice(), 2,
                              QPointF(200, 100), QPointF(200, 100),
                              view.mapToGlobal(QPointF(200, 100)), 0.5, QPointF());
    QApplication::sendEvent(&view, &pinch);
    QCOMPARE(view.zoom(), fitted * 1.5);
    QNativeGestureEvent other(Qt::RotateNativeGesture, QPointingDevice::primaryPointingDevice(), 2,
                              QPointF(200, 100), QPointF(200, 100),
                              view.mapToGlobal(QPointF(200, 100)), 0.5, QPointF());
    QApplication::sendEvent(&view, &other);
    QCOMPARE(view.zoom(), fitted * 1.5);
}

void PhotoViewTest::outlinesAreDrawnOverTheImage() {
    PhotoView view;
    view.resize(400, 200);
    QImage image(400, 200, QImage::Format_RGB32);
    image.fill(Qt::black);
    view.setImage(image);
    std::vector<QPolygonF> outlines;
    outlines.push_back(QPolygonF{QPointF(50, 50), QPointF(150, 50), QPointF(150, 150)});
    view.setOutlines(std::move(outlines), 0);
    const QImage grabbed = view.grab().toImage();
    QVERIFY(qRed(grabbed.pixel(100, 50)) > 100);
    QCOMPARE(qRed(grabbed.pixel(300, 100)), 0);
}

QTEST_MAIN(PhotoViewTest)
#include "photo_view_test.moc"
