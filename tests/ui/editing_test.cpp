#include <QAction>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QListWidget>
#include <QMenu>
#include <QPolygonF>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

#include "core/stitch_plan.hpp"
#include "stitch/decisions.hpp"
#include "stitch/pose_file.hpp"
#include "stitch_fixture.hpp"
#include "street_test_helpers.hpp"
#include "test_helpers.hpp"
#include "ui/main_window.hpp"
#include "ui/stitch_job.hpp"

using streetpano::core::Patch;
using streetpano::core::StitchOptions;
using streetpano::core::WallPoint;
using streetpano::stitch::PoseFile;
using streetpano::stitch::readDecisions;
using streetpano::stitch::readPoseFile;
using streetpano::stitch::stitchPanorama;
using streetpano::stitch::StitchResult;
using streetpano::stitch::withPatches;
using streetpano::test::kPhotos;
using streetpano::test::squareAt;
using streetpano::test::stitchInWindow;
using streetpano::test::Street;
using streetpano::test::StreetFixture;
using streetpano::test::wallSquare;
using streetpano::ui::MainWindow;
using streetpano::ui::StitchJob;

// Editing the panorama in the window: marking areas, the menu and the edits panel. These are
// apart from the stitcher tests so that each program stays well within CI's time limit.
class EditingTest : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase();
    void startsOnTheProjectsStep();
    void stepsBecomeAvailableAsTheStreetProgresses();
    void eachStepShowsOnlyItsOwnTools();
    void aProjectIsCreatedOpenedAndRemembered();
    void aProjectWhosePhotosAreGoneIsReported();
    void exportSavesThePanoramaWithItsEdits();
    void alignShowsThePhotosSideBySide();
    void guidesAreAddedMovedAndRemoved();
    void alignWithoutCameraPositionsExplainsWhatToDo();
    void markingAnAreaSavesADecision();
    void editsCanBeUndoneAndRedone();
    void changingAPhotoStepsThroughTheChoices();
    void clickingSelectsAnArea();
    void deletingAnAreaRestoresThePanorama();
    void editsComeBackAfterStitchingAgain();
    void editingIsOnlyForThePanorama();
    void savesThePanoramaWithItsEdits();
    void anOutlineChangesNothingUntilAChoiceIsMade();
    void removeUsesThePhotoThatShowsTheAreaClean();
    void keepUsesThePhotoLookingStraightAtTheArea();
    void removeWorksOnAnAreaAlreadyMade();
    void cancelDropsAnOutline();
    void aDescriptionIsSavedAndChangesNothing();
    void anEmptyDescriptionIsIgnored();
    void theAreaMenuOffersTheChoices();
    void rightClickSelectsAnArea();
    void tooFewPhotosCantRemove();
    void severalAreasCanBeDescribedInARow();
    void escapeStopsMarkingThenCancels();
    void aLineCanOnlyBeDescribed();
    void aNewOutlineIsListedStraightAway();
    void noSingleKeyDeletesAnEdit();
    void outlinesWithoutAChoiceAreKept();
    void choosingAShapeTurnsMarkingOn();
    void finishedEditsAreNotOutlined();
    void theEditsPanelListsEveryEdit();
    void selectingInThePanelSelectsTheArea();
    void untickingAnEditSwitchesItOff();
    void editsCanBeReordered();
    void thePanelIsEmptyAwayFromThePanorama();
};

void EditingTest::markingAnAreaSavesADecision() {
    const Street street;
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    QVERIFY(window.isShowingPanorama());
    QVERIFY(window.stitchMessage().contains(QStringLiteral("100% detail")));
    QVERIFY(window.markArea(squareAt(window.panorama())));
    QCOMPARE(window.areaCount(), 1);
    QCOMPARE(window.selectedArea(), 0);
    QVERIFY(!window.selectedAreaPhoto().isEmpty());
    const auto saved = readDecisions(window.decisionsPath());
    QVERIFY(saved.decisions.has_value());
    QCOMPARE(saved.decisions->patches.size(), std::size_t{1});
    QCOMPARE(QString::fromStdString(saved.decisions->patches[0].photo), window.selectedAreaPhoto());
    QVERIFY(window.positionText().contains(QStringLiteral("Area 1 of 1")));
}

void EditingTest::editsCanBeUndoneAndRedone() {
    const Street street;
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    QVERIFY(!window.undo());
    QVERIFY(!window.redo());
    QVERIFY(window.markArea(squareAt(window.panorama())));
    QVERIFY(window.undo());
    QCOMPARE(window.areaCount(), 0);
    QVERIFY(readDecisions(window.decisionsPath()).decisions->patches.empty());
    QVERIFY(window.redo());
    QCOMPARE(window.areaCount(), 1);
    QCOMPARE(readDecisions(window.decisionsPath()).decisions->patches.size(), std::size_t{1});
    // A new edit replaces what could have been redone.
    QVERIFY(window.undo());
    QVERIFY(window.markArea(squareAt(window.panorama(), 20)));
    QVERIFY(!window.redo());
}

void EditingTest::changingAPhotoStepsThroughTheChoices() {
    const Street street;
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    QVERIFY(!window.changeAreaPhoto(1));  // Nothing is selected yet.
    QVERIFY(window.markArea(squareAt(window.panorama())));
    const QString first = window.selectedAreaPhoto();
    QVERIFY(window.changeAreaPhoto(1));
    QVERIFY(window.selectedAreaPhoto() != first);
    QVERIFY(window.changeAreaPhoto(-1));
    QCOMPARE(window.selectedAreaPhoto(), first);
    QVERIFY(window.changeAreaPhoto(-1));
    QVERIFY(window.selectedAreaPhoto() != first);
}

void EditingTest::clickingSelectsAnArea() {
    const Street street;
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    const QPolygonF square = squareAt(window.panorama());
    QVERIFY(window.markArea(square));
    window.selectAreaAt(QPointF(1.0, 1.0));
    QCOMPARE(window.selectedArea(), -1);
    QVERIFY(window.selectedAreaPhoto().isEmpty());
    QVERIFY(!window.deleteSelectedArea());
    window.selectAreaAt(square.boundingRect().center());
    QCOMPARE(window.selectedArea(), 0);
}

void EditingTest::deletingAnAreaRestoresThePanorama() {
    const Street street;
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    const QImage before = window.panorama();
    QVERIFY(window.markArea(squareAt(before)));
    QVERIFY(window.deleteSelectedArea());
    QCOMPARE(window.areaCount(), 0);
    QCOMPARE(window.selectedArea(), -1);
    QCOMPARE(window.panorama(), before);
}

void EditingTest::editsComeBackAfterStitchingAgain() {
    const Street street;
    QString photo;
    QString decisions;
    {
        MainWindow window;
        QVERIFY(stitchInWindow(window, street));
        QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
        QVERIFY(window.markArea(squareAt(window.panorama())));
        photo = window.selectedAreaPhoto();
        decisions = window.decisionsPath();
    }
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    QCOMPARE(window.areaCount(), 1);
    QCOMPARE(window.decisionsPath(), decisions);
    window.selectAreaAt(squareAt(window.panorama()).boundingRect().center());
    QCOMPARE(window.selectedAreaPhoto(), photo);
}

void EditingTest::editingIsOnlyForThePanorama() {
    const Street street;
    MainWindow window;
    QVERIFY(window.openFolder(street.folder.path()));
    QVERIFY(!window.markAreaAction()->isEnabled());
    QVERIFY(window.startStitch(street.posesPath));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    QVERIFY(window.markAreaAction()->isEnabled());
    window.markAreaAction()->setChecked(true);
    QVERIFY(window.photoView()->isLassoMode());
    window.selectPhoto(1);
    QVERIFY(!window.isShowingPanorama());
    QVERIFY(!window.markAreaAction()->isEnabled());
    QVERIFY(!window.photoView()->isLassoMode());
    QVERIFY(!window.markArea(QPolygonF{QPointF(0, 0), QPointF(10, 0), QPointF(10, 10)}));
    window.selectAreaAt(QPointF(1, 1));
    QCOMPARE(window.selectedArea(), -1);
}

void EditingTest::savesThePanoramaWithItsEdits() {
    const Street street;
    MainWindow window;
    QVERIFY(!window.savePanoramaFile());  // Nothing to save yet.
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    QVERIFY(window.markArea(squareAt(window.panorama())));
    QVERIFY(QFile::remove(window.panoramaPath()));
    QVERIFY(window.savePanoramaFile());
    QVERIFY(QFile::exists(window.panoramaPath()));
    QVERIFY(!QImage(window.panoramaPath()).isNull());
}

void EditingTest::anOutlineChangesNothingUntilAChoiceIsMade() {
    const Street street;
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    const QImage before = window.panorama();
    window.markAreaAction()->setChecked(true);
    QVERIFY(window.editMessage().contains(QStringLiteral("Esc")));
    window.beginArea(squareAt(before));
    QVERIFY(window.hasPendingArea());
    // Marking stays on, so several objects can be outlined one after another.
    QVERIFY(window.markAreaAction()->isChecked());
    QCOMPARE(window.areaCount(), 0);
    QCOMPARE(window.panorama(), before);
    QVERIFY(!QFile::exists(window.decisionsPath()));
}

void EditingTest::removeUsesThePhotoThatShowsTheAreaClean() {
    const Street street;
    // This photo looks straight at the area, but has something in the way.
    QImage red(StreetFixture::kWidth, StreetFixture::kHeight, QImage::Format_RGB32);
    red.fill(qRgb(250, 10, 10));
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    const QPolygonF square = squareAt(window.panorama());
    QVERIFY(window.markArea(square));
    const QString straight = window.selectedAreaPhoto();
    QVERIFY(window.deleteSelectedArea());
    QVERIFY(red.save(QDir(street.folder.path()).filePath(straight)));

    window.beginArea(square);
    QVERIFY(window.removeObject());
    QVERIFY(!window.hasPendingArea());
    QCOMPARE(window.areaCount(), 1);
    QCOMPARE(window.selectedArea(), 0);
    QVERIFY(window.selectedAreaPhoto() != straight);
    QVERIFY(window.editMessage().contains(QStringLiteral("Removed")));
    // Undoing goes back to before the area existed.
    QVERIFY(window.undo());
    QCOMPARE(window.areaCount(), 0);
}

void EditingTest::keepUsesThePhotoLookingStraightAtTheArea() {
    const Street street;
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    const QPolygonF square = squareAt(window.panorama());
    QVERIFY(window.markArea(square));
    const QString straight = window.selectedAreaPhoto();
    QVERIFY(window.deleteSelectedArea());
    window.beginArea(square);
    QVERIFY(window.keepObject());
    QCOMPARE(window.selectedAreaPhoto(), straight);
    QVERIFY(window.editMessage().contains(QStringLiteral("Kept")));
}

void EditingTest::removeWorksOnAnAreaAlreadyMade() {
    const Street street;
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    QVERIFY(!window.removeObject());  // Nothing is marked yet.
    QVERIFY(!window.keepObject());
    QVERIFY(window.markArea(squareAt(window.panorama())));
    QVERIFY(window.removeObject());
    QVERIFY(window.keepObject());
    QCOMPARE(window.areaCount(), 1);
}

void EditingTest::cancelDropsAnOutline() {
    const Street street;
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    QVERIFY(!window.cancelOrDeleteArea());
    window.beginArea(squareAt(window.panorama()));
    QVERIFY(window.cancelOrDeleteArea());
    QVERIFY(!window.hasPendingArea());
    QCOMPARE(window.areaCount(), 0);
    QVERIFY(window.markArea(squareAt(window.panorama())));
    QVERIFY(window.cancelOrDeleteArea());
    QCOMPARE(window.areaCount(), 0);
}

void EditingTest::aDescriptionIsSavedAndChangesNothing() {
    const Street street;
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    const QImage before = window.panorama();
    window.beginArea(squareAt(before));
    QVERIFY(window.describeFix(QStringLiteral("  remove the white van ")));
    QCOMPARE(window.areaCount(), 1);
    QCOMPARE(window.selectedAreaNote(), QStringLiteral("remove the white van"));
    QVERIFY(window.selectedAreaPhoto().isEmpty());
    QCOMPARE(window.panorama(), before);
    QVERIFY(window.editMessage().contains(QStringLiteral("No AI")));
    QVERIFY(window.positionText().contains(QStringLiteral("waiting for the AI")));
    const auto saved = readDecisions(window.decisionsPath());
    QCOMPARE(saved.decisions->patches[0].note, std::string("remove the white van"));
    // It can be changed, and comes back after stitching again.
    QVERIFY(window.describeFix(QStringLiteral("remove the van")));
    QCOMPARE(window.selectedAreaNote(), QStringLiteral("remove the van"));
    QCOMPARE(window.areaCount(), 1);
}

void EditingTest::anEmptyDescriptionIsIgnored() {
    const Street street;
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    window.beginArea(squareAt(window.panorama()));
    QVERIFY(!window.describeFix(QStringLiteral("   ")));
    QVERIFY(window.hasPendingArea());
    QVERIFY(window.selectedAreaNote().isEmpty());
}

void EditingTest::theAreaMenuOffersTheChoices() {
    const Street street;
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    QStringList labels;
    for (const QAction* action : window.areaMenu()->actions()) {
        labels.append(action->text());
    }
    const QString all = labels.join(u' ');
    QVERIFY(all.contains(QStringLiteral("Remove")));
    QVERIFY(all.contains(QStringLiteral("Keep")));
    QVERIFY(all.contains(QStringLiteral("Different Photo")));
    QVERIFY(all.contains(QStringLiteral("AI Not Built Yet")));
    QVERIFY(all.contains(QStringLiteral("Cancel")));
    // Drawing an outline in the view opens the menu by itself.
    streetpano::ui::PhotoView* view = window.photoView();
    view->resize(800, 400);
    window.markAreaAction()->setChecked(true);
    emit view->lassoFinished(squareAt(window.panorama()));
    QVERIFY(window.hasPendingArea());
    QTRY_VERIFY(window.areaMenu()->isVisible());  // Once the mouse release has finished.
    window.areaMenu()->hide();
}

void EditingTest::rightClickSelectsAnArea() {
    const Street street;
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    const QPolygonF square = squareAt(window.panorama());
    QVERIFY(window.markArea(square));
    streetpano::ui::PhotoView* view = window.photoView();
    // Right-clicking away from every area explains what to do.
    window.selectAreaAt(QPointF(1, 1));
    emit view->imageRightClicked(QPointF(1, 1), QPoint(0, 0));
    QVERIFY(!window.areaMenu()->isVisible());
    QVERIFY(window.editMessage().contains(QStringLiteral("Mark an Area")));
    emit view->imageRightClicked(square.boundingRect().center(), QPoint(0, 0));
    QCOMPARE(window.selectedArea(), 0);
    QVERIFY(window.areaMenu()->isVisible());
    window.areaMenu()->hide();
    // A pending outline stays when it's the one clicked, and goes when another place is.
    window.beginArea(square);
    emit view->imageRightClicked(square.boundingRect().center(), QPoint(0, 0));
    QVERIFY(window.hasPendingArea());
    window.areaMenu()->hide();
    emit view->imageRightClicked(QPointF(1, 1), QPoint(0, 0));
    QVERIFY(!window.hasPendingArea());
    window.selectPhoto(1);
    emit view->imageRightClicked(square.boundingRect().center(), QPoint(0, 0));
    QVERIFY(!window.areaMenu()->isVisible());
}

void EditingTest::tooFewPhotosCantRemove() {
    const Street street(2);
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    window.beginArea(squareAt(window.panorama()));
    QVERIFY(!window.removeObject());
    QVERIFY(window.editMessage().contains(QStringLiteral("Too few")));
    QVERIFY(window.hasPendingArea());
}

void EditingTest::severalAreasCanBeDescribedInARow() {
    const Street street;
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    window.markAreaAction()->setChecked(true);
    for (int round = 1; round <= 3; ++round) {
        emit window.photoView()->lassoFinished(squareAt(window.panorama(), 10 * round));
        QVERIFY(window.markAreaAction()->isChecked());
        QVERIFY(window.describeFix(QStringLiteral("note %1").arg(round)));
        window.areaMenu()->hide();
    }
    QCOMPARE(window.areaCount(), 3);
    QCOMPARE(readDecisions(window.decisionsPath()).decisions->patches[2].note,
             std::string("note 3"));
}

void EditingTest::escapeStopsMarkingThenCancels() {
    const Street street;
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    window.markAreaAction()->setChecked(true);
    window.beginArea(squareAt(window.panorama()));
    QTest::keyClick(&window, Qt::Key_Escape);
    QVERIFY(!window.markAreaAction()->isChecked());
    QVERIFY(window.hasPendingArea());
    QTest::keyClick(&window, Qt::Key_Escape);
    QVERIFY(!window.hasPendingArea());
    QTest::keyClick(&window, Qt::Key_Escape);  // Nothing left to stop.
}

void EditingTest::aLineCanOnlyBeDescribed() {
    const Street street;
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    const double y = window.panorama().height() / 2.0;
    const QPolygonF line{QPointF(20, y), QPointF(220, y + 4)};
    window.beginArea(line);
    QVERIFY(window.hasPendingArea());
    QVERIFY(window.editMessage().contains(QStringLiteral("line")));
    QVERIFY(!window.removeObject());
    QVERIFY(!window.keepObject());
    QVERIFY(window.describeFix(QStringLiteral("should be exactly horizontal")));
    QVERIFY(window.editsList()->item(0)->text().startsWith(QStringLiteral("1. Line")));
    const auto saved = readDecisions(window.decisionsPath());
    QVERIFY(saved.decisions.has_value());
    QCOMPARE(saved.decisions->patches[0].outline.size(), std::size_t{2});
    // A click near the line selects it, and one far from it doesn't.
    window.selectAreaAt(QPointF(120, y + 2));
    QCOMPARE(window.selectedArea(), 0);
    window.selectAreaAt(QPointF(120, y + 200));
    QCOMPARE(window.selectedArea(), -1);
    window.selectAreaAt(QPointF(400, y));  // Beyond its end.
    QCOMPARE(window.selectedArea(), -1);
}

void EditingTest::aNewOutlineIsListedStraightAway() {
    const Street street;
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    QVERIFY(window.describeFix(QStringLiteral("not yet")) == false);
    window.beginArea(squareAt(window.panorama()));
    QCOMPARE(window.editsList()->count(), 1);
    QVERIFY(window.editsList()->item(0)->text().contains(QStringLiteral("New area")));
    QCOMPARE(window.editsList()->currentRow(), 0);
    QVERIFY(window.hasPendingArea());  // Selecting its row keeps it.
    QVERIFY(window.describeFix(QStringLiteral("first")));
    QVERIFY(window.editsList()->item(0)->text().contains(QStringLiteral("first")));
    const double y = window.panorama().height() / 2.0;
    window.beginArea(QPolygonF{QPointF(20, y), QPointF(220, y)});
    QCOMPARE(window.editsList()->count(), 2);
    QVERIFY(window.editsList()->item(1)->text().contains(QStringLiteral("New line")));
    // Right-clicking that row offers the choices, for the new outline.
    emit window.editsList()->customContextMenuRequested(
        window.editsList()->visualItemRect(window.editsList()->item(1)).center());
    QVERIFY(window.hasPendingArea());
    QTRY_VERIFY(window.areaMenu()->isVisible());
    window.areaMenu()->hide();
    QVERIFY(window.describeFix(QStringLiteral("second")));
    QCOMPARE(window.areaCount(), 2);
    QCOMPARE(window.editsList()->count(), 2);
}

void EditingTest::noSingleKeyDeletesAnEdit() {
    const Street street;
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    QVERIFY(window.markArea(squareAt(window.panorama())));
    window.editsList()->setFocus();
    QTest::keyClick(&window, Qt::Key_Backspace);
    QTest::keyClick(window.editsList(), Qt::Key_Backspace);
    QTest::keyClick(window.editsList(), Qt::Key_Delete);
    QCOMPARE(window.areaCount(), 1);
}

void EditingTest::outlinesWithoutAChoiceAreKept() {
    const Street street;
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    const QImage before = window.panorama();
    window.beginArea(squareAt(before));
    window.beginArea(squareAt(before, 20));
    window.beginArea(squareAt(before, 10));
    QCOMPARE(window.areaCount(), 2);  // And the newest is still waiting for a choice.
    QVERIFY(window.hasPendingArea());
    QVERIFY(window.editsList()->item(0)->text().contains(QStringLiteral("no choice yet")));
    QCOMPARE(window.editsList()->count(), 3);
    QCOMPARE(window.panorama(), before);  // Marking alone changes nothing.
    const auto saved = readDecisions(window.decisionsPath());
    QVERIFY(saved.decisions.has_value());
    QCOMPARE(saved.decisions->patches[0].kind, std::string("marked"));
    // A marked area can be described or removed later.
    window.selectArea(0);
    QVERIFY(window.describeFix(QStringLiteral("rebuild the entrance")));
    QVERIFY(window.editsList()->item(0)->text().contains(QStringLiteral("rebuild the entrance")));
    window.selectArea(1);
    QVERIFY(window.removeObject());
    QVERIFY(window.editsList()->item(1)->text().contains(QStringLiteral("Remove")));
    QVERIFY(window.undo());
    QVERIFY(window.undo());
    QVERIFY(window.undo());
    QCOMPARE(window.areaCount(), 1);
}

void EditingTest::choosingAShapeTurnsMarkingOn() {
    const Street street;
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    window.setMarkShape(streetpano::ui::PhotoView::MarkShape::Line);
    QVERIFY(window.markAreaAction()->isChecked());
    QVERIFY(window.photoView()->markShape() == streetpano::ui::PhotoView::MarkShape::Line);
    window.setMarkShape(streetpano::ui::PhotoView::MarkShape::Rectangle);
    QVERIFY(window.photoView()->markShape() == streetpano::ui::PhotoView::MarkShape::Rectangle);
    window.selectPhoto(1);
    window.setMarkShape(streetpano::ui::PhotoView::MarkShape::Freehand);
    QVERIFY(!window.markAreaAction()->isChecked());  // Only on the panorama.
}

void EditingTest::finishedEditsAreNotOutlined() {
    const Street street;
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    auto* view = window.photoView();
    QVERIFY(window.memoryText().contains(QStringLiteral("panorama:")));
    window.beginArea(squareAt(window.panorama()));
    QCOMPARE(view->outlineCount(), std::size_t{1});  // The one being worked on.
    QVERIFY(window.removeObject());
    QCOMPARE(view->outlineCount(), std::size_t{1});  // Still selected.
    window.selectAreaAt(QPointF(1, 1));
    QCOMPARE(view->outlineCount(), std::size_t{0});  // Finished, so no line.
    window.setOutlinesShown(true);
    QVERIFY(window.showOutlinesAction()->isChecked());
    QCOMPARE(view->outlineCount(), std::size_t{1});
    window.setOutlinesShown(false);
    QCOMPARE(view->outlineCount(), std::size_t{0});
}

void EditingTest::theEditsPanelListsEveryEdit() {
    const Street street;
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    QCOMPARE(window.editsList()->count(), 0);
    window.beginArea(squareAt(window.panorama()));
    QVERIFY(window.removeObject());
    window.beginArea(squareAt(window.panorama(), 20));
    QVERIFY(window.keepObject());
    window.beginArea(squareAt(window.panorama(), 10));
    QVERIFY(window.describeFix(QStringLiteral("repair the car")));
    QCOMPARE(window.editsList()->count(), 3);
    QVERIFY(window.editsList()->item(0)->text().contains(QStringLiteral("Remove")));
    QVERIFY(window.editsList()->item(1)->text().contains(QStringLiteral("Keep whole")));
    QVERIFY(window.editsList()->item(2)->text().contains(QStringLiteral("repair the car")));
    QVERIFY(window.editsList()->item(2)->text().contains(QStringLiteral("no AI yet")));
    QCOMPARE(window.editsList()->currentRow(), 2);
    // What was chosen is saved, so the panel shows the same after stitching again.
    const auto saved = readDecisions(window.decisionsPath());
    QCOMPARE(saved.decisions->patches[0].kind, std::string("remove"));
    QCOMPARE(saved.decisions->patches[1].kind, std::string("keep"));
    QCOMPARE(saved.decisions->patches[2].kind, std::string("describe"));
}

void EditingTest::selectingInThePanelSelectsTheArea() {
    const Street street;
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    QVERIFY(window.markArea(squareAt(window.panorama())));
    QVERIFY(window.markArea(squareAt(window.panorama(), 20)));
    window.photoView()->resize(800, 400);
    window.editsList()->setCurrentRow(0);
    QCOMPARE(window.selectedArea(), 0);
    QVERIFY(!window.photoView()->isFitted());  // Zoomed to the area.
    window.selectArea(-1);
    QCOMPARE(window.selectedArea(), -1);
    window.selectArea(5);  // There's no such area, so nothing changes.
    QCOMPARE(window.selectedArea(), -1);
}

void EditingTest::untickingAnEditSwitchesItOff() {
    const Street street;
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    const QImage before = window.panorama();
    QVERIFY(window.markArea(squareAt(before)));
    const QImage edited = window.panorama();
    QVERIFY(edited != before);
    window.editsList()->item(0)->setCheckState(Qt::Unchecked);
    QCOMPARE(window.panorama(), before);
    QCOMPARE(window.areaCount(), 1);  // Still there, only switched off.
    QVERIFY(readDecisions(window.decisionsPath()).decisions->patches[0].hidden);
    QCOMPARE(window.editsList()->item(0)->checkState(), Qt::Unchecked);
    QVERIFY(!window.setAreaHidden(0, true));  // Already off.
    QVERIFY(!window.setAreaHidden(3, true));
    QVERIFY(window.undo());
    QCOMPARE(window.panorama(), edited);
    // Choosing again switches it back on.
    QVERIFY(window.setAreaHidden(0, true));
    QVERIFY(window.removeObject());
    QVERIFY(window.editsList()->item(0)->checkState() == Qt::Checked);
}

void EditingTest::editsCanBeReordered() {
    const Street street;
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    window.beginArea(squareAt(window.panorama()));
    QVERIFY(window.removeObject());
    window.beginArea(squareAt(window.panorama(), 20));
    QVERIFY(window.describeFix(QStringLiteral("second")));
    QVERIFY(window.moveArea(1, 0));
    QVERIFY(window.editsList()->item(0)->text().contains(QStringLiteral("second")));
    QCOMPARE(window.selectedArea(), 0);
    QCOMPARE(readDecisions(window.decisionsPath()).decisions->patches[0].note,
             std::string("second"));
    QVERIFY(!window.moveArea(0, 0));
    QVERIFY(!window.moveArea(0, 7));
    QVERIFY(window.undo());
    QVERIFY(window.editsList()->item(1)->text().contains(QStringLiteral("second")));
    // Dragging a row in the panel moves the edit the same way.
    auto* model = window.editsList()->model();
    QVERIFY(model->moveRow(QModelIndex(), 1, QModelIndex(), 0));
    QTRY_VERIFY(window.editsList()->item(0)->text().contains(QStringLiteral("second")));
    QCOMPARE(readDecisions(window.decisionsPath()).decisions->patches[0].note,
             std::string("second"));
}

void EditingTest::thePanelIsEmptyAwayFromThePanorama() {
    const Street street;
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    QVERIFY(window.markArea(squareAt(window.panorama())));
    QCOMPARE(window.editsList()->count(), 1);
    window.selectPhoto(2);
    QCOMPARE(window.editsList()->count(), 0);
    window.selectArea(0);
    QCOMPARE(window.selectedArea(), -1);
    window.showPanorama();
    QCOMPARE(window.editsList()->count(), 1);
}

void EditingTest::initTestCase() { QStandardPaths::setTestModeEnabled(true); }

void EditingTest::startsOnTheProjectsStep() {
    MainWindow window;
    QVERIFY(window.step() == MainWindow::Step::Projects);
    QVERIFY(window.stepAction(MainWindow::Step::Projects)->isChecked());
    for (const auto step : {MainWindow::Step::Photos, MainWindow::Step::Align,
                            MainWindow::Step::Edit, MainWindow::Step::Export}) {
        QVERIFY(!window.stepAction(step)->isEnabled());
        window.setStep(step);  // Not available yet, so nothing happens.
        QVERIFY(window.step() == MainWindow::Step::Projects);
    }
}

void EditingTest::stepsBecomeAvailableAsTheStreetProgresses() {
    const Street street;
    MainWindow window;
    QVERIFY(window.openFolder(street.folder.path()));
    QVERIFY(window.step() == MainWindow::Step::Photos);
    QVERIFY(window.stepAction(MainWindow::Step::Align)->isEnabled());
    QVERIFY(!window.stepAction(MainWindow::Step::Edit)->isEnabled());
    QVERIFY(window.startStitch(street.posesPath));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    QVERIFY(window.step() == MainWindow::Step::Edit);
    QVERIFY(!window.positionText().contains(QStringLiteral("Photo 1 of")));
    QVERIFY(window.stepAction(MainWindow::Step::Export)->isEnabled());
    window.setStep(MainWindow::Step::Photos);
    QVERIFY(!window.isShowingPanorama());
    QVERIFY(window.stepAction(MainWindow::Step::Photos)->isChecked());
    window.setStep(MainWindow::Step::Align);
    QVERIFY(window.step() == MainWindow::Step::Align);
    window.setStep(MainWindow::Step::Edit);
    QVERIFY(window.isShowingPanorama());
    window.setStep(MainWindow::Step::Projects);
    QVERIFY(window.step() == MainWindow::Step::Projects);
    window.showPanorama();
    QVERIFY(window.step() == MainWindow::Step::Edit);
}

void EditingTest::eachStepShowsOnlyItsOwnTools() {
    const Street street;
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    window.markAreaAction()->setChecked(true);
    window.setStep(MainWindow::Step::Photos);
    QVERIFY(window.filmStrip()->isVisible());
    QVERIFY(!window.markAreaAction()->isChecked());  // Marking is only for the panorama.
    QVERIFY(!window.editsList()->isVisible());
    window.setStep(MainWindow::Step::Edit);
    QVERIFY(!window.filmStrip()->isVisible());
    QVERIFY(window.editsList()->isVisible());
}

void EditingTest::aProjectIsCreatedOpenedAndRemembered() {
    const Street street;
    {
        MainWindow window;
        QVERIFY(window.createProject(QStringLiteral("Test Street"), street.folder.path()));
        QVERIFY(window.project().has_value());
        QCOMPARE(window.project()->name, QStringLiteral("Test Street"));
        QVERIFY(window.windowTitle().startsWith(QStringLiteral("Test Street")));
        QVERIFY(window.step() == MainWindow::Step::Photos);
        QCOMPARE(window.photoCount(), kPhotos);
    }
    MainWindow window;
    QVERIFY(window.recentProjectsList()->count() >= 1);
    const QString first = window.recentProjectsList()->item(0)->text();
    QVERIFY(first.startsWith(QStringLiteral("Test Street")));
    emit window.recentProjectsList()->itemActivated(window.recentProjectsList()->item(0));
    QCOMPARE(window.project()->name, QStringLiteral("Test Street"));
    // Opening by folder finds the same project.
    MainWindow again;
    QVERIFY(again.openProjectForFolder(street.folder.path()));
    QCOMPARE(again.project()->name, QStringLiteral("Test Street"));
    QVERIFY(QDir(street.folder.path() + QStringLiteral(".streetpano")).removeRecursively());
}

void EditingTest::aProjectWhosePhotosAreGoneIsReported() {
    MainWindow window;
    QVERIFY(!window.createProject(QStringLiteral("x"), QStringLiteral("/no/such/folder")));
    QVERIFY(window.projectsMessage().contains(QStringLiteral("isn't a folder")));
    QVERIFY(!window.openProject(QStringLiteral("/no/such/project.json")));
    QTemporaryDir root;
    const QString photos = QDir(root.path()).filePath(QStringLiteral("empty"));
    QVERIFY(QDir().mkpath(photos));
    QVERIFY(!window.createProject(QStringLiteral("Empty"), photos));
    QVERIFY(window.projectsMessage().contains(QStringLiteral("aren't in")));
    QVERIFY(QDir(photos + QStringLiteral(".streetpano")).removeRecursively());
}

void EditingTest::exportSavesThePanoramaWithItsEdits() {
    const Street street;
    MainWindow window;
    QVERIFY(!window.savePanoramaAs(QStringLiteral("/tmp/none.jpg")));  // Nothing yet.
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    QVERIFY(window.markArea(squareAt(window.panorama())));
    window.setStep(MainWindow::Step::Export);
    QVERIFY(window.step() == MainWindow::Step::Export);
    const QString path = street.folder.filePath(QStringLiteral("export.png"));
    QVERIFY(window.savePanoramaAs(path));
    QCOMPARE(QImage(path).size(), window.panorama().size());
    QVERIFY(!window.savePanoramaAs(street.folder.filePath(QStringLiteral("no/such/x.jpg"))));
}

void EditingTest::alignShowsThePhotosSideBySide() {
    const Street street;
    MainWindow window;
    QVERIFY(window.openFolder(street.folder.path()));
    window.setStep(MainWindow::Step::Align);
    QVERIFY(window.step() == MainWindow::Step::Align);
    QTRY_VERIFY_WITH_TIMEOUT(!window.isPreparingAlignment(), 30000);
    QCOMPARE(static_cast<int>(window.alignmentView().tiles.size()), kPhotos);
    QVERIFY(!window.alignView()->image().isNull());
    // Stitching from here goes to the Edit step.
    QVERIFY(window.startStitchFromFolder());
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    QVERIFY(window.step() == MainWindow::Step::Edit);
    // Coming back doesn't prepare the photos again.
    window.setStep(MainWindow::Step::Align);
    QVERIFY(!window.isPreparingAlignment());
}

void EditingTest::guidesAreAddedMovedAndRemoved() {
    const Street street;
    MainWindow window;
    QVERIFY(window.openFolder(street.folder.path()));
    QVERIFY(!window.addGuide());  // Nothing to put it on yet.
    window.setStep(MainWindow::Step::Align);
    QTRY_VERIFY_WITH_TIMEOUT(!window.isPreparingAlignment(), 30000);
    QVERIFY(window.addGuide());
    QVERIFY(window.addGuide());
    QCOMPARE(window.guideCount(), std::size_t{2});
    QCOMPARE(window.alignView()->guides().size(), std::size_t{2});
    QVERIFY(window.moveGuide(0, 30.0));
    QCOMPARE(window.alignView()->guides().front(), 30.0);
    QVERIFY(QFile::exists(window.alignmentPath()));
    // Guides are saved, and come back when the street is opened again.
    MainWindow again;
    QVERIFY(again.openFolder(street.folder.path()));
    again.setStep(MainWindow::Step::Align);
    QTRY_VERIFY_WITH_TIMEOUT(!again.isPreparingAlignment(), 30000);
    QCOMPARE(again.guideCount(), std::size_t{2});
    QVERIFY(std::abs(again.alignView()->guides().front() - 30.0) < 1e-6);
    // Dragging a guide off the photos, or right-clicking it, removes it.
    QVERIFY(again.moveGuide(0, -5.0));
    QCOMPARE(again.guideCount(), std::size_t{1});
    emit again.alignView()->imageRightClicked(QPointF(10, again.alignView()->guides().front()),
                                              QPoint());
    QCOMPARE(again.guideCount(), std::size_t{0});
    QVERIFY(!again.moveGuide(3, 10.0));
    QVERIFY(!again.removeGuide(-1));
    // The grid can be switched off and on.
    again.setStep(MainWindow::Step::Align);
    QVERIFY(QFile::remove(again.alignmentPath()));
}

void EditingTest::alignWithoutCameraPositionsExplainsWhatToDo() {
    QTemporaryDir folder;
    for (int index = 0; index < 3; ++index) {
        streetpano::test::StreetFixture::writePhoto(folder, index);
    }
    MainWindow window;
    QVERIFY(window.openFolder(folder.path()));
    window.setStep(MainWindow::Step::Align);
    QVERIFY(!window.isPreparingAlignment());
    QVERIFY(window.alignView()->message().contains(QStringLiteral("stitch.sh")));
}

QTEST_MAIN(EditingTest)
#include "editing_test.moc"
