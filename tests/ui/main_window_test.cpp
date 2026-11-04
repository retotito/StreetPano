#include "ui/main_window.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "test_helpers.hpp"
#include "ui/film_strip_delegate.hpp"

using streetpano::test::writePhoto;
using streetpano::ui::FilmStripDelegate;
using streetpano::ui::MainWindow;
using streetpano::ui::PhotoListModel;

namespace {

// Returns an invalid colour until a photo is showing.
QColor shownColor(const MainWindow& window) {
    const QImage& image = window.photoView()->image();
    return image.isNull() ? QColor() : image.pixelColor(1, 1);
}

}  // namespace

class MainWindowTest : public QObject {
    Q_OBJECT

  private slots:
    void startsWithAnInvitation();
    void opensAFolderAndShowsTheFirstPhoto();
    void movesThroughPhotos();
    void ignoresPositionsOutsideTheStreet();
    void arrowKeysMoveAlongTheStreet();
    void reportsAMissingFolder();
    void reportsAFolderWithoutPhotos();
    void showsAnErrorForADamagedPhoto();
    void aNewFolderReplacesResultsFromTheOldOne();
    void loadsOnlyVisibleThumbnails();
    void thumbnailsAppearInTheModel();
    void filmStripDrawsFullSizeThumbnails();
    void filmStripMarksDamagedPhotos();
    void previewMemoryStaysWithinItsBudget();
    void closingWhileLoadingIsSafe();
};

void MainWindowTest::startsWithAnInvitation() {
    const MainWindow window;
    QCOMPARE(window.photoCount(), 0);
    QCOMPARE(window.currentPhoto(), -1);
    QVERIFY(window.photoView()->message().contains(QStringLiteral("Open a folder")));
    QCOMPARE(window.positionText(), QStringLiteral("No photos open"));
}

void MainWindowTest::opensAFolderAndShowsTheFirstPhoto() {
    const QTemporaryDir folder;
    writePhoto(folder, QStringLiteral("img10.png"), Qt::blue);
    writePhoto(folder, QStringLiteral("img2.png"), Qt::red);
    MainWindow window;
    QVERIFY(window.openFolder(folder.path()));
    QCOMPARE(window.photoCount(), 2);
    QCOMPARE(window.currentPhoto(), 0);
    QTRY_VERIFY(!window.photoView()->image().isNull());
    QCOMPARE(shownColor(window), QColor(Qt::red));
    QCOMPARE(window.positionText(), QStringLiteral("Photo 1 of 2: img2.png"));
}

void MainWindowTest::movesThroughPhotos() {
    const QTemporaryDir folder;
    writePhoto(folder, QStringLiteral("1.png"), Qt::red);
    writePhoto(folder, QStringLiteral("2.png"), Qt::green);
    writePhoto(folder, QStringLiteral("3.png"), Qt::blue);
    MainWindow window;
    window.openFolder(folder.path());
    window.selectPhoto(2);
    QCOMPARE(window.currentPhoto(), 2);
    QTRY_COMPARE(shownColor(window), QColor(Qt::blue));
    window.selectPhoto(1);
    QTRY_COMPARE(shownColor(window), QColor(Qt::green));
    // Going back to a photo that's still cached shows it straight away.
    QTRY_VERIFY(window.previewCacheUsedBytes() > 0);
    window.selectPhoto(2);
    QCOMPARE(shownColor(window), QColor(Qt::blue));
}

void MainWindowTest::ignoresPositionsOutsideTheStreet() {
    const QTemporaryDir folder;
    writePhoto(folder, QStringLiteral("1.png"), Qt::red);
    MainWindow window;
    window.openFolder(folder.path());
    window.selectPhoto(-1);
    window.selectPhoto(5);
    QCOMPARE(window.currentPhoto(), 0);
}

void MainWindowTest::arrowKeysMoveAlongTheStreet() {
    const QTemporaryDir folder;
    writePhoto(folder, QStringLiteral("1.png"), Qt::red);
    writePhoto(folder, QStringLiteral("2.png"), Qt::green);
    MainWindow window;
    window.openFolder(folder.path());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.activateWindow();
    window.photoView()->setFocus();
    QTest::keyClick(&window, Qt::Key_Right);
    QTRY_COMPARE(window.currentPhoto(), 1);
    QTest::keyClick(&window, Qt::Key_Left);
    QTRY_COMPARE(window.currentPhoto(), 0);
}

void MainWindowTest::reportsAMissingFolder() {
    const QTemporaryDir folder;
    MainWindow window;
    QVERIFY(!window.openFolder(folder.filePath(QStringLiteral("missing"))));
    QVERIFY(window.photoView()->message().contains(QStringLiteral("Can't open folder")));
    QCOMPARE(window.photoCount(), 0);
}

void MainWindowTest::reportsAFolderWithoutPhotos() {
    const QTemporaryDir folder;
    MainWindow window;
    QVERIFY(!window.openFolder(folder.path()));
    QVERIFY(window.photoView()->message().contains(QStringLiteral("no photos")));
}

void MainWindowTest::showsAnErrorForADamagedPhoto() {
    const QTemporaryDir folder;
    QFile bad(folder.filePath(QStringLiteral("1.png")));
    QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("not an image");
    bad.close();
    writePhoto(folder, QStringLiteral("2.png"), Qt::green);

    MainWindow window;
    window.openFolder(folder.path());
    QTRY_VERIFY(window.photoView()->message().contains(QStringLiteral("Can't read this photo")));

    // The rest of the street still works.
    window.selectPhoto(1);
    QTRY_COMPARE(shownColor(window), QColor(Qt::green));
}

void MainWindowTest::aNewFolderReplacesResultsFromTheOldOne() {
    const QTemporaryDir first;
    const QTemporaryDir second;
    for (int index = 0; index < 5; ++index) {
        writePhoto(first, QStringLiteral("%1.png").arg(index), Qt::red, 1500, 1500);
    }
    writePhoto(second, QStringLiteral("0.png"), Qt::green, 1500, 1500);

    MainWindow window;
    window.openFolder(first.path());
    window.openFolder(second.path());
    QTRY_VERIFY(!window.photoView()->image().isNull());
    QTest::qWait(200);
    QCOMPARE(shownColor(window), QColor(Qt::green));
    QCOMPARE(window.photoCount(), 1);
}

void MainWindowTest::loadsOnlyVisibleThumbnails() {
    const QTemporaryDir folder;
    constexpr int kPhotos = 300;
    for (int index = 0; index < kPhotos; ++index) {
        writePhoto(folder, QStringLiteral("%1.png").arg(index), Qt::gray, 32, 18);
    }
    MainWindow window;
    window.resize(800, 600);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openFolder(folder.path());
    QTest::qWait(300);
    QVERIFY(window.photoModel()->requestedCount() > 0);
    QVERIFY(window.photoModel()->requestedCount() < kPhotos / 2);
}

void MainWindowTest::thumbnailsAppearInTheModel() {
    const QTemporaryDir folder;
    writePhoto(folder, QStringLiteral("1.png"), Qt::red, 1000, 500);
    MainWindow window;
    window.openFolder(folder.path());
    PhotoListModel* model = window.photoModel();
    const QModelIndex item = model->index(0);
    QTRY_VERIFY(model->data(item, Qt::DecorationRole).isValid());
    const auto thumbnail = model->data(item, Qt::DecorationRole).value<QImage>();
    QCOMPARE(thumbnail.width(), PhotoListModel::kThumbnailSide);
    QCOMPARE(model->data(item, Qt::DisplayRole).toString(), QStringLiteral("1.png"));
    QVERIFY(model->data(item, Qt::ToolTipRole).toString().endsWith(QStringLiteral("1.png")));
    QVERIFY(!model->data(item, Qt::UserRole).isValid());
    QVERIFY(!model->data(QModelIndex(), Qt::DisplayRole).isValid());
    QCOMPARE(model->rowCount(item), 0);
    QVERIFY(model->pathAt(99).isEmpty());
}

void MainWindowTest::filmStripDrawsFullSizeThumbnails() {
    // A regression test. Thumbnails were once squashed into a thin line, because the first
    // item was measured before its image had loaded.
    const QTemporaryDir folder;
    writePhoto(folder, QStringLiteral("1.png"), Qt::red, 1600, 900);
    writePhoto(folder, QStringLiteral("2.png"), Qt::blue, 1600, 900);
    writePhoto(folder, QStringLiteral("3.png"), Qt::green, 1600, 900);
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openFolder(folder.path());

    QListView* strip = window.filmStrip();
    const QModelIndex second = window.photoModel()->index(1);
    QTRY_VERIFY(window.photoModel()->data(second, Qt::DecorationRole).isValid());
    const QRect cell = strip->visualRect(second);
    QCOMPARE(cell.height(), FilmStripDelegate::kThumbnailHeight +
                                FilmStripDelegate::kCaptionHeight +
                                2 * FilmStripDelegate::kPadding);

    const qreal scale = strip->viewport()->devicePixelRatioF();
    const QPoint low = (cell.topLeft() + QPoint(FilmStripDelegate::kPadding + 96,
                                                FilmStripDelegate::kPadding + 100)) *
                       scale;
    QTRY_COMPARE(strip->viewport()->grab().toImage().pixelColor(low), QColor(Qt::blue));
}

void MainWindowTest::filmStripMarksDamagedPhotos() {
    const QTemporaryDir folder;
    QFile bad(folder.filePath(QStringLiteral("1.png")));
    QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("not an image");
    bad.close();
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openFolder(folder.path());
    const QModelIndex item = window.photoModel()->index(0);
    QTRY_VERIFY(window.photoModel()->data(item, PhotoListModel::kHasErrorRole).toBool());
    QVERIFY(!window.photoModel()->data(item, Qt::DecorationRole).isValid());
    QVERIFY(!window.filmStrip()->viewport()->grab().isNull());
}

void MainWindowTest::previewMemoryStaysWithinItsBudget() {
    const QTemporaryDir folder;
    constexpr std::size_t kBudget = std::size_t{10} * 1024 * 1024;
    for (int index = 0; index < 8; ++index) {
        writePhoto(folder, QStringLiteral("%1.png").arg(index), Qt::gray, 1000, 1000);
    }
    MainWindow window(kBudget);
    window.openFolder(folder.path());
    for (int index = 0; index < 8; ++index) {
        window.selectPhoto(index);
        QTRY_VERIFY(!window.photoView()->image().isNull());
        QTest::qWait(30);
        QVERIFY(window.previewCacheUsedBytes() <= kBudget);
    }
    QVERIFY(window.previewCacheUsedBytes() > 0);
}

void MainWindowTest::closingWhileLoadingIsSafe() {
    const QTemporaryDir folder;
    for (int index = 0; index < 6; ++index) {
        writePhoto(folder, QStringLiteral("%1.png").arg(index), Qt::gray, 1500, 1500);
    }
    for (int attempt = 0; attempt < 5; ++attempt) {
        MainWindow window;
        window.openFolder(folder.path());
    }
}

QTEST_MAIN(MainWindowTest)
#include "main_window_test.moc"
