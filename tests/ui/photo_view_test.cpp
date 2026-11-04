#include "ui/photo_view.hpp"

#include <QImage>
#include <QtTest>

using streetpano::ui::PhotoView;

class PhotoViewTest : public QObject {
    Q_OBJECT

  private slots:
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

QTEST_MAIN(PhotoViewTest)
#include "photo_view_test.moc"
