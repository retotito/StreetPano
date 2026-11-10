#include "stitch/stitcher.hpp"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QListWidget>
#include <QMenu>
#include <QPolygonF>
#include <QSignalSpy>
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
using streetpano::test::StreetFixture;
using streetpano::ui::MainWindow;
using streetpano::ui::StitchJob;

class StitcherTest : public QObject {
    Q_OBJECT

  private slots:
    void matchesTheTrueFacade();
    void matchesTheTrueFacadeWhenReduced();
    void alignsAWallThatStartsTooNear();
    void patchesAreDrawnFromTheOriginalPhoto();
    void aPatchWithAnUnknownPhotoGivesAWarning();
    void aPatchWithAMissingPhotoGivesAWarning();
    void noPatchesLeaveTheBaseAsItIs();
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
    void choosingAShapeTurnsMarkingOn();
    void finishedEditsAreNotOutlined();
    void theEditsPanelListsEveryEdit();
    void selectingInThePanelSelectsTheArea();
    void untickingAnEditSwitchesItOff();
    void editsCanBeReordered();
    void thePanelIsEmptyAwayFromThePanorama();
    void hidesExposureDifferencesAtSeams();
    void reportsProgress();
    void respectsTheMemoryBudget();
    void canBeCancelled();
    void skipsMissingAndDamagedPhotos();
    void skipsPhotosOfADifferentSize();
    void failsClearlyWithoutPhotos();
    void failsClearlyWithoutPoints();
    void jobDeliversAResult();
    void jobSignalsArriveOnTheOwnerThread();
    void jobReportsABadFile();
    void jobCanBeDestroyedWhileRunning();
    void windowStitchesAndShowsThePanorama();
    void windowReportsAFailedStitch();
    void windowRefusesAStitchWithoutPhotos();
    void openingAFolderStopsAStitch();
    void showsThePanoramaAgainAfterBrowsing();
    void findsCameraPositionsNextToTheFolder();
    void stitchFromFolderFailsWithoutCameraPositions();
};

namespace {

constexpr int kPhotos = 12;

struct Street {
    QTemporaryDir folder;
    QString posesPath;

    explicit Street(int count = kPhotos) {
        for (int index = 0; index < count; ++index) {
            StreetFixture::writePhoto(folder, index);
        }
        posesPath = StreetFixture::writePoses(folder, StreetFixture::poses(count));
    }

    [[nodiscard]] PoseFile poses() const { return *readPoseFile(posesPath).file; }
};

// Compares a panorama with the true facade colours, ignoring the outermost pixels.
double meanError(const QImage& panorama, const PoseFile& poses, const StitchOptions& options) {
    const auto surface = *streetpano::core::fitFacadeSurface(poses.poses, poses.points).surface;
    const auto plan =
        *streetpano::core::makeStitchPlan(surface, poses.poses, poses.focalPixels,
                                          StreetFixture::kWidth, StreetFixture::kHeight, options)
             .plan;
    double total = 0.0;
    int count = 0;
    for (int y = 4; y < panorama.height() - 4; ++y) {
        for (int x = 4; x < panorama.width() - 4; ++x) {
            const double u = plan.leftArc + (x + 0.5) * plan.unitsPerPixel;
            const double v = plan.topHeight - (y + 0.5) * plan.unitsPerPixel;
            const auto point = surface.pointAt(u, v);
            const QRgb truth = StreetFixture::facadeColor(point.x, point.y);
            const QRgb found = panorama.pixel(x, y);
            total += std::abs(qRed(truth) - qRed(found)) + std::abs(qGreen(truth) - qGreen(found)) +
                     std::abs(qBlue(truth) - qBlue(found));
            count += 3;
        }
    }
    return total / count;
}

}  // namespace

void StitcherTest::matchesTheTrueFacade() {
    const Street street;
    const auto result = stitchPanorama(street.poses(), QDir(street.folder.path()));
    QVERIFY(result.error.isEmpty());
    QVERIFY(!result.panorama.isNull());
    QCOMPARE(result.scale, 1.0);
    const double error = meanError(result.panorama, street.poses(), {});
    QVERIFY2(error < 2.0, qPrintable(QStringLiteral("Mean error: %1").arg(error)));
}

void StitcherTest::alignsAWallThatStartsTooNear() {
    const Street street;
    auto poses = street.poses();
    for (auto& point : poses.points) {
        point.z = StreetFixture::kDepth * 0.9;
    }
    // This facade's pattern is coarse, so a wider spacing makes the wrong distance visible.
    std::vector<streetpano::core::CameraPose> spaced;
    for (std::size_t index = 0; index < poses.poses.size(); index += 4) {
        spaced.push_back(poses.poses[index]);
    }
    poses.poses = spaced;
    const auto result = stitchPanorama(poses, QDir(street.folder.path()));
    QVERIFY(result.error.isEmpty());
    QVERIFY(result.alignment.seams > 0);
    QVERIFY(result.alignment.moved > 0);
}

void StitcherTest::matchesTheTrueFacadeWhenReduced() {
    const Street street;
    StitchOptions options;
    options.maxPixels = 40000;
    const auto result = stitchPanorama(street.poses(), QDir(street.folder.path()), options);
    QVERIFY(result.scale < 1.0);
    const double error = meanError(result.panorama, street.poses(), options);
    QVERIFY2(error < 8.0, qPrintable(QStringLiteral("Mean error: %1").arg(error)));
}

void StitcherTest::hidesExposureDifferencesAtSeams() {
    const auto stitchWith = [](double darker) {
        QTemporaryDir folder;
        for (int index = 0; index < 4; ++index) {
            StreetFixture::writePhoto(folder, index, index % 2 == 0 ? darker : 1.0);
        }
        const auto poses =
            *readPoseFile(StreetFixture::writePoses(folder, StreetFixture::poses(4))).file;
        return stitchPanorama(poses, QDir(folder.path()));
    };
    const auto largestStep = [](const QImage& image) {
        int largest = 0;
        const int y = image.height() / 2;
        for (int x = 1; x < image.width(); ++x) {
            largest = qMax(largest, qAbs(qGray(image.pixel(x, y)) - qGray(image.pixel(x - 1, y))));
        }
        return largest;
    };
    // Compared with evenly lit photos, a 15% difference must leave no visible step.
    const int even = largestStep(stitchWith(1.0).panorama);
    const int uneven = largestStep(stitchWith(0.85).panorama);
    QVERIFY2(uneven <= even + 4, qPrintable(QStringLiteral("%1 and %2").arg(even).arg(uneven)));
}

void StitcherTest::reportsProgress() {
    const Street street;
    int last = 0;
    int total = 0;
    const auto result =
        stitchPanorama(street.poses(), QDir(street.folder.path()), {}, [&](int done, int count) {
            last = done;
            total = count;
        });
    QVERIFY(!result.panorama.isNull());
    QCOMPARE(last, kPhotos);
    QCOMPARE(total, kPhotos);
}

void StitcherTest::respectsTheMemoryBudget() {
    const Street street;
    StitchOptions options;
    options.maxPixels = 20000;
    const auto result = stitchPanorama(street.poses(), QDir(street.folder.path()), options);
    QVERIFY(result.panorama.width() * result.panorama.height() <= 20000);
}

void StitcherTest::canBeCancelled() {
    const Street street;
    const std::atomic<bool> cancel{true};
    const auto result = stitchPanorama(street.poses(), QDir(street.folder.path()), {}, {}, &cancel);
    QVERIFY(result.panorama.isNull());
    QVERIFY(result.error.contains(QStringLiteral("cancelled")));
}

void StitcherTest::skipsMissingAndDamagedPhotos() {
    const Street street;
    QVERIFY(QFile::remove(street.folder.filePath(QStringLiteral("003.png"))));
    QFile damaged(street.folder.filePath(QStringLiteral("006.png")));
    QVERIFY(damaged.open(QIODevice::WriteOnly));
    damaged.write("not an image");
    damaged.close();

    const auto result = stitchPanorama(street.poses(), QDir(street.folder.path()));
    QVERIFY(!result.panorama.isNull());
    QCOMPARE(result.warnings.size(), 2);
    QVERIFY(result.warnings.first().contains(QStringLiteral("003.png")));
    // Neighbouring photos cover the gaps, so the facade is still correct.
    QVERIFY(meanError(result.panorama, street.poses(), {}) < 2.0);
}

void StitcherTest::skipsPhotosOfADifferentSize() {
    const Street street;
    QImage other(50, 50, QImage::Format_RGB32);
    other.fill(Qt::red);
    QVERIFY(other.save(street.folder.filePath(QStringLiteral("005.png"))));
    const auto result = stitchPanorama(street.poses(), QDir(street.folder.path()));
    QCOMPARE(result.warnings.size(), 1);
    QVERIFY(result.warnings.first().contains(QStringLiteral("different size")));
    QVERIFY(!result.panorama.isNull());
}

void StitcherTest::failsClearlyWithoutPhotos() {
    const Street street;
    const QTemporaryDir empty;
    const auto result = stitchPanorama(street.poses(), QDir(empty.path()));
    QVERIFY(result.panorama.isNull());
    QVERIFY(result.error.contains(QStringLiteral("None of the photos")));
}

void StitcherTest::failsClearlyWithoutPoints() {
    const Street street;
    PoseFile poses = street.poses();
    poses.points.clear();
    const auto result = stitchPanorama(poses, QDir(street.folder.path()));
    QVERIFY(result.panorama.isNull());
    QVERIFY(result.error.contains(QStringLiteral("too few")));
}

void StitcherTest::jobDeliversAResult() {
    const Street street;
    const QString output = street.folder.filePath(QStringLiteral("panorama.png"));
    StitchJob job(street.posesPath, QDir(street.folder.path()), output);
    QSignalSpy finished(&job, &StitchJob::finished);
    job.start();
    job.start();  // Starting twice must be harmless.
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
    const auto result = job.takeResult();
    QVERIFY(!result.panorama.isNull());
    QCOMPARE(job.done(), kPhotos);
    QCOMPARE(job.total(), kPhotos);
    QVERIFY(QFile::exists(output));
    QCOMPARE(job.outputPath(), output);
}

void StitcherTest::jobSignalsArriveOnTheOwnerThread() {
    const Street street;
    StitchJob job(street.posesPath, QDir(street.folder.path()),
                  street.folder.filePath(QStringLiteral("out.png")));
    // Direct connections run on the emitting thread, which must be this one.
    bool alwaysOwnerThread = true;
    bool finished = false;
    const auto check = [&] { alwaysOwnerThread &= QThread::currentThread() == thread(); };
    connect(&job, &StitchJob::progressChanged, this, check, Qt::DirectConnection);
    connect(
        &job, &StitchJob::finished, this,
        [&] {
            check();
            finished = true;
        },
        Qt::DirectConnection);
    job.start();
    QTRY_VERIFY_WITH_TIMEOUT(finished, 30000);
    QVERIFY(alwaysOwnerThread);
}

void StitcherTest::jobReportsABadFile() {
    const QTemporaryDir folder;
    StitchJob job(folder.filePath(QStringLiteral("missing.json")), QDir(folder.path()),
                  folder.filePath(QStringLiteral("out.png")));
    QSignalSpy finished(&job, &StitchJob::finished);
    job.start();
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(job.takeResult().error.contains(QStringLiteral("Can't open")));
}

void StitcherTest::jobCanBeDestroyedWhileRunning() {
    const Street street;
    for (int attempt = 0; attempt < 5; ++attempt) {
        StitchJob job(street.posesPath, QDir(street.folder.path()),
                      street.folder.filePath(QStringLiteral("out.png")));
        job.start();
    }
    const StitchJob neverStarted(street.posesPath, QDir(street.folder.path()), QString());
    QVERIFY(neverStarted.outputPath().isEmpty());
}

namespace {

// A square of panorama pixels near the middle.
QPolygonF squareAt(const QImage& panorama, int size = 40) {
    const double x = panorama.width() / 2.0;
    const double y = panorama.height() / 2.0;
    return QPolygonF{QPointF(x, y), QPointF(x + size, y), QPointF(x + size, y + size),
                     QPointF(x, y + size)};
}

std::vector<WallPoint> wallSquare(const StitchResult& result, const QPolygonF& polygon) {
    std::vector<WallPoint> wall;
    for (const QPointF& point : polygon) {
        wall.push_back({result.plan.leftArc + point.x() * result.plan.unitsPerPixel,
                        result.plan.topHeight - point.y() * result.plan.unitsPerPixel});
    }
    return wall;
}

bool stitchInWindow(MainWindow& window, const Street& street) {
    return window.openFolder(street.folder.path()) && window.startStitch(street.posesPath);
}

}  // namespace

void StitcherTest::patchesAreDrawnFromTheOriginalPhoto() {
    const Street street;
    auto result = stitchPanorama(street.poses(), QDir(street.folder.path()));
    QVERIFY(result.error.isEmpty());
    // A photo of solid red, in place of the one the patch takes its pixels from.
    QImage red(StreetFixture::kWidth, StreetFixture::kHeight, QImage::Format_RGB32);
    red.fill(qRgb(250, 10, 10));
    QVERIFY(red.save(QDir(street.folder.path()).filePath(QStringLiteral("005.png"))));

    const QPolygonF square = squareAt(result.base);
    const Patch patch{wallSquare(result, square), "005.png", {}, {}, false};
    QStringList warnings;
    const QImage patched = withPatches(result, QDir(street.folder.path()), {&patch, 1}, warnings);
    QVERIFY(warnings.isEmpty());
    const QPointF middle = square.boundingRect().center();
    QVERIFY(qRed(patched.pixel(middle.toPoint())) > 200);
    QVERIFY(qGreen(patched.pixel(middle.toPoint())) < 60);
    // Outside the area, and in the base, nothing has changed.
    QCOMPARE(patched.pixel(10, 10), result.base.pixel(10, 10));
    QVERIFY(qRed(result.base.pixel(middle.toPoint())) != qRed(patched.pixel(middle.toPoint())));
}

void StitcherTest::aPatchWithAnUnknownPhotoGivesAWarning() {
    const Street street;
    const auto result = stitchPanorama(street.poses(), QDir(street.folder.path()));
    const Patch patch{wallSquare(result, squareAt(result.base)), "not-here.png", {}, {}, false};
    QStringList warnings;
    const QImage patched = withPatches(result, QDir(street.folder.path()), {&patch, 1}, warnings);
    QCOMPARE(warnings.size(), 1);
    QCOMPARE(patched, result.base);
}

void StitcherTest::aPatchWithAMissingPhotoGivesAWarning() {
    const Street street;
    const auto result = stitchPanorama(street.poses(), QDir(street.folder.path()));
    QVERIFY(QFile::remove(QDir(street.folder.path()).filePath(QStringLiteral("005.png"))));
    const Patch patch{wallSquare(result, squareAt(result.base)), "005.png", {}, {}, false};
    QStringList warnings;
    const QImage patched = withPatches(result, QDir(street.folder.path()), {&patch, 1}, warnings);
    QCOMPARE(warnings.size(), 1);
    QCOMPARE(patched, result.base);
}

void StitcherTest::noPatchesLeaveTheBaseAsItIs() {
    const Street street;
    const auto result = stitchPanorama(street.poses(), QDir(street.folder.path()));
    QStringList warnings;
    const QImage same = withPatches(result, QDir(street.folder.path()), {}, warnings);
    QCOMPARE(same, result.base);
}

void StitcherTest::markingAnAreaSavesADecision() {
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

void StitcherTest::editsCanBeUndoneAndRedone() {
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

void StitcherTest::changingAPhotoStepsThroughTheChoices() {
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

void StitcherTest::clickingSelectsAnArea() {
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

void StitcherTest::deletingAnAreaRestoresThePanorama() {
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

void StitcherTest::editsComeBackAfterStitchingAgain() {
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

void StitcherTest::editingIsOnlyForThePanorama() {
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

void StitcherTest::savesThePanoramaWithItsEdits() {
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

void StitcherTest::anOutlineChangesNothingUntilAChoiceIsMade() {
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

void StitcherTest::removeUsesThePhotoThatShowsTheAreaClean() {
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

void StitcherTest::keepUsesThePhotoLookingStraightAtTheArea() {
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

void StitcherTest::removeWorksOnAnAreaAlreadyMade() {
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

void StitcherTest::cancelDropsAnOutline() {
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

void StitcherTest::aDescriptionIsSavedAndChangesNothing() {
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

void StitcherTest::anEmptyDescriptionIsIgnored() {
    const Street street;
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    window.beginArea(squareAt(window.panorama()));
    QVERIFY(!window.describeFix(QStringLiteral("   ")));
    QVERIFY(window.hasPendingArea());
    QVERIFY(window.selectedAreaNote().isEmpty());
}

void StitcherTest::theAreaMenuOffersTheChoices() {
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
    QVERIFY(window.areaMenu()->isVisible());
    window.areaMenu()->hide();
}

void StitcherTest::rightClickSelectsAnArea() {
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

void StitcherTest::tooFewPhotosCantRemove() {
    const Street street(2);
    MainWindow window;
    QVERIFY(stitchInWindow(window, street));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    window.beginArea(squareAt(window.panorama()));
    QVERIFY(!window.removeObject());
    QVERIFY(window.editMessage().contains(QStringLiteral("Too few")));
    QVERIFY(window.hasPendingArea());
}

void StitcherTest::finishedEditsAreNotOutlined() {
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

void StitcherTest::theEditsPanelListsEveryEdit() {
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

void StitcherTest::selectingInThePanelSelectsTheArea() {
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

void StitcherTest::untickingAnEditSwitchesItOff() {
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

void StitcherTest::editsCanBeReordered() {
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

void StitcherTest::thePanelIsEmptyAwayFromThePanorama() {
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

void StitcherTest::severalAreasCanBeDescribedInARow() {
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

void StitcherTest::escapeStopsMarkingThenCancels() {
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

void StitcherTest::aLineCanOnlyBeDescribed() {
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

void StitcherTest::choosingAShapeTurnsMarkingOn() {
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

void StitcherTest::windowStitchesAndShowsThePanorama() {
    const Street street;
    MainWindow window;
    QVERIFY(window.openFolder(street.folder.path()));
    QVERIFY(window.startStitch(street.posesPath));
    QVERIFY(window.isStitching());
    QVERIFY(!window.startStitch(street.posesPath));  // Only one at a time.
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    QVERIFY(!window.panorama().isNull());
    QCOMPARE(window.photoView()->image().size(), window.panorama().size());
    QVERIFY(window.stitchMessage().contains(QStringLiteral("100% detail")));
    QVERIFY(window.positionText().contains(QStringLiteral("Panorama")));
    QVERIFY(QFile::exists(window.panoramaPath()));
}

void StitcherTest::windowReportsAFailedStitch() {
    const Street street;
    MainWindow window;
    window.openFolder(street.folder.path());
    QVERIFY(window.startStitch(street.folder.filePath(QStringLiteral("missing.json"))));
    QTRY_VERIFY(!window.isStitching());
    QVERIFY(window.panorama().isNull());
    QVERIFY(window.stitchMessage().contains(QStringLiteral("failed")));
}

void StitcherTest::windowRefusesAStitchWithoutPhotos() {
    MainWindow window;
    QVERIFY(!window.startStitch(QStringLiteral("poses.json")));
}

void StitcherTest::openingAFolderStopsAStitch() {
    const Street street;
    const QTemporaryDir other;
    streetpano::test::writePhoto(other, QStringLiteral("1.png"), Qt::green);
    MainWindow window;
    window.openFolder(street.folder.path());
    window.startStitch(street.posesPath);
    window.openFolder(other.path());
    QVERIFY(!window.isStitching());
    QTest::qWait(100);
    QVERIFY(window.panorama().isNull());
    QTRY_VERIFY(!window.photoView()->image().isNull());
    QCOMPARE(window.photoView()->image().pixelColor(1, 1), QColor(Qt::green));
}

void StitcherTest::showsThePanoramaAgainAfterBrowsing() {
    const Street street;
    MainWindow window;
    window.openFolder(street.folder.path());
    window.startStitch(street.posesPath);
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    window.selectPhoto(3);
    QTRY_COMPARE(window.photoView()->image().width(), StreetFixture::kWidth);
    window.showPanorama();
    QCOMPARE(window.photoView()->image().size(), window.panorama().size());
}

void StitcherTest::findsCameraPositionsNextToTheFolder() {
    const QTemporaryDir parent;
    const QString photos = parent.filePath(QStringLiteral("street"));
    QVERIFY(QDir().mkpath(photos));
    QVERIFY(QDir().mkpath(photos + QStringLiteral(".streetpano")));
    const Street street(3);
    for (const QString& name :
         {QStringLiteral("000.png"), QStringLiteral("001.png"), QStringLiteral("002.png")}) {
        QVERIFY(QFile::copy(street.folder.filePath(name), photos + u'/' + name));
    }
    QVERIFY(QFile::copy(street.posesPath, photos + QStringLiteral(".streetpano/poses.json")));

    MainWindow window;
    QVERIFY(window.openFolder(photos));
    QVERIFY(window.startStitchFromFolder());
    QTRY_VERIFY_WITH_TIMEOUT(!window.isStitching(), 30000);
    QVERIFY(!window.panorama().isNull());
    QVERIFY(window.panoramaPath().endsWith(QStringLiteral(".streetpano/panorama.jpg")));
}

void StitcherTest::stitchFromFolderFailsWithoutCameraPositions() {
    QTemporaryDir folder;
    StreetFixture::writePhoto(folder, 0);
    MainWindow window;
    QVERIFY(window.openFolder(folder.path()));
    QVERIFY(!window.startStitchFromFolder());
}

QTEST_MAIN(StitcherTest)
#include "stitcher_test.moc"
