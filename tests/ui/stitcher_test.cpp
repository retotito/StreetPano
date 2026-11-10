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
#include "stitch/alignment.hpp"
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

class StitcherTest : public QObject {
    Q_OBJECT

  private slots:
    void matchesTheTrueFacade();
    void matchesTheTrueFacadeWhenReduced();
    void alignsAWallThatStartsTooNear();
    void patchesAreDrawnFromTheOriginalPhoto();
    void thePhotosAreShownSideBySide();
    void aHeightIsTheSameRowInEveryPhoto();
    void aStreetThatCantBePreparedSaysWhy();
    void guidesAreSavedAndReadBack();
    void correctionsAreSavedAndReadBack();
    void correctionsChangeTheStitch();
    void aTileCanBeRedrawnWithACorrection();
    void damagedGuidesAreRefused();
    void aPatchWithAnUnknownPhotoGivesAWarning();
    void aPatchWithAMissingPhotoGivesAWarning();
    void noPatchesLeaveTheBaseAsItIs();
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

namespace {}  // namespace

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

void StitcherTest::thePhotosAreShownSideBySide() {
    const Street street;
    const auto prepared =
        streetpano::stitch::prepareStreet(street.poses(), QDir(street.folder.path()));
    QVERIFY(prepared.error.isEmpty());
    int calls = 0;
    const auto view = streetpano::stitch::buildAlignmentView(prepared, QDir(street.folder.path()),
                                                             120, [&calls](int, int) { ++calls; });
    QVERIFY(view.error.isEmpty());
    QCOMPARE(static_cast<int>(view.tiles.size()), kPhotos);
    QCOMPARE(calls, kPhotos);
    QCOMPARE(view.strip.height(), 120);
    // Tiles follow each other, with a gap between them.
    for (std::size_t index = 1; index < view.tiles.size(); ++index) {
        QVERIFY(view.tiles[index].left > view.tiles[index - 1].left + view.tiles[index - 1].width);
    }
    QCOMPARE(view.strip.width(),
             view.tiles.back().left + view.tiles.back().width +
                 (view.tiles[1].left - view.tiles[0].left - view.tiles[0].width));
    // The middle of a tile shows the photo, and the gap shows the background.
    const auto& tile = view.tiles[3];
    QVERIFY(view.strip.pixel(tile.left + tile.width / 2, 60) !=
            view.strip.pixel(tile.left - 2, 60));
}

void StitcherTest::aHeightIsTheSameRowInEveryPhoto() {
    const Street street;
    const auto prepared =
        streetpano::stitch::prepareStreet(street.poses(), QDir(street.folder.path()));
    const auto view =
        streetpano::stitch::buildAlignmentView(prepared, QDir(street.folder.path()), 120);
    QCOMPARE(view.rowForHeight(view.heightForRow(37.5)), 37.5);
    QCOMPARE(view.heightForRow(0.0), view.topHeight);
    // The fixture's facade is level, so a feature at one height is on one row in every tile.
    const auto rowOfColour = [&view](const streetpano::stitch::AlignmentTile& tile) {
        const QRgb wanted = view.strip.pixel(view.tiles[0].left + view.tiles[0].width / 2, 60);
        int best = -1;
        int bestDifference = 1000;
        for (int y = 50; y < 70; ++y) {
            const QRgb found = view.strip.pixel(tile.left + tile.width / 2, y);
            const int difference = std::abs(qGreen(found) - qGreen(wanted));
            if (difference < bestDifference) {
                bestDifference = difference;
                best = y;
            }
        }
        return best;
    };
    // Green only changes with height in this fixture, so it marks rows.
    QCOMPARE(rowOfColour(view.tiles[5]), rowOfColour(view.tiles[0]));
}

void StitcherTest::aStreetThatCantBePreparedSaysWhy() {
    const QTemporaryDir empty;
    const Street street;
    const auto prepared = streetpano::stitch::prepareStreet(street.poses(), QDir(empty.path()));
    QVERIFY(prepared.error.contains(QStringLiteral("None of the photos")));
    const auto view = streetpano::stitch::buildAlignmentView(prepared, QDir(empty.path()), 120);
    QVERIFY(view.strip.isNull());
    QCOMPARE(view.error, prepared.error);
    QVERIFY(!streetpano::stitch::buildAlignmentView({}, QDir(empty.path()), 120).error.isEmpty());
    // Cancelling stops before the first photo.
    const auto ready =
        streetpano::stitch::prepareStreet(street.poses(), QDir(street.folder.path()));
    const std::atomic<bool> cancel{true};
    QVERIFY(
        streetpano::stitch::buildAlignmentView(ready, QDir(street.folder.path()), 120, {}, &cancel)
            .error.contains(QStringLiteral("Cancelled")));
}

void StitcherTest::guidesAreSavedAndReadBack() {
    const QTemporaryDir folder;
    const QString path =
        streetpano::stitch::alignmentPathFor(folder.filePath(QStringLiteral("poses.json")));
    QCOMPARE(path, folder.filePath(QStringLiteral("alignment.json")));
    const auto missing = streetpano::stitch::readAlignment(path);
    QVERIFY(missing.alignment.has_value());
    QVERIFY(missing.alignment->guides.empty());
    QVERIFY(streetpano::stitch::writeAlignment({{1.5, -2.25}, {}}, path));
    const auto read = streetpano::stitch::readAlignment(path);
    QCOMPARE(read.alignment->guides, (std::vector<double>{1.5, -2.25}));
    QVERIFY(!streetpano::stitch::writeAlignment({}, folder.filePath(QStringLiteral("no/a.json"))));
}

void StitcherTest::correctionsAreSavedAndReadBack() {
    const QTemporaryDir folder;
    const QString path = folder.filePath(QStringLiteral("alignment.json"));
    streetpano::stitch::Alignment alignment;
    alignment.photos["003.png"] = {1.5, -0.4, 0.25, 1.02};
    alignment.photos["004.png"] = {};  // Neutral, so it isn't written.
    QVERIFY(streetpano::stitch::writeAlignment(alignment, path));
    const auto read = streetpano::stitch::readAlignment(path);
    QVERIFY(read.alignment.has_value());
    QCOMPARE(read.alignment->photos.size(), std::size_t{1});
    const auto adjustment = read.alignment->adjustmentFor("003.png");
    QCOMPARE(adjustment.rollDegrees, 1.5);
    QCOMPARE(adjustment.pitchDegrees, -0.4);
    QCOMPARE(adjustment.shift, 0.25);
    QCOMPARE(adjustment.scale, 1.02);
    QVERIFY(read.alignment->adjustmentFor("009.png").isNeutral());
    // Impossible corrections are refused.
    for (const QByteArray& photo :
         {QByteArray(R"("a.png": {"scale": 0})"), QByteArray(R"("a.png": {"roll_degrees": 90})"),
          QByteArray(R"("../a.png": {"shift": 1})")}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(R"({"format_version": 1, "guides": [], "photos": {)" + photo + "}}");
        file.close();
        QVERIFY(!streetpano::stitch::readAlignment(path).alignment.has_value());
    }
}

void StitcherTest::correctionsChangeTheStitch() {
    const Street street;
    const auto plain = stitchPanorama(street.poses(), QDir(street.folder.path()));
    streetpano::stitch::Alignment alignment;
    alignment.photos["005.png"] = {3.0, 0.0, 0.0, 1.0};
    const auto corrected =
        stitchPanorama(street.poses(), QDir(street.folder.path()), {}, {}, nullptr, &alignment);
    QVERIFY(corrected.error.isEmpty());
    QVERIFY(corrected.panorama != plain.panorama);
    // Only the corrected photo moved. The wall comes from the original positions.
    QCOMPARE(corrected.surface.medianCameraDistance, plain.surface.medianCameraDistance);
}

void StitcherTest::aTileCanBeRedrawnWithACorrection() {
    const Street street;
    const auto prepared =
        streetpano::stitch::prepareStreet(street.poses(), QDir(street.folder.path()));
    auto view = streetpano::stitch::buildAlignmentView(prepared, QDir(street.folder.path()), 120);
    QVERIFY(!view.original.isNull());
    QCOMPARE(view.original.size(), view.strip.size());
    const QImage before = view.strip.copy();
    streetpano::stitch::redrawTile(view, prepared, QDir(street.folder.path()), 4,
                                   {0.0, 0.0, 10.0 * view.unitsPerPixel, 1.0});
    const auto& tile = view.tiles[4];
    QVERIFY(view.strip.copy(tile.left, 0, tile.width, 120) !=
            before.copy(tile.left, 0, tile.width, 120));
    // Its neighbours aren't touched.
    const auto& next = view.tiles[5];
    QCOMPARE(view.strip.copy(next.left, 0, next.width, 120),
             before.copy(next.left, 0, next.width, 120));
    // Redrawing without a correction gives the original tile again.
    streetpano::stitch::redrawTile(view, prepared, QDir(street.folder.path()), 4, {});
    QCOMPARE(view.strip, before);
    streetpano::stitch::redrawTile(view, prepared, QDir(street.folder.path()), 99, {});  // Ignored.
    QCOMPARE(streetpano::stitch::tileAt(view, tile.left + 1.0), 4);
    QCOMPARE(streetpano::stitch::tileAt(view, tile.left - 2.0), -1);  // The gap.
    QCOMPARE(streetpano::stitch::tileAt(view, -5.0), -1);
}

void StitcherTest::damagedGuidesAreRefused() {
    const QTemporaryDir folder;
    const QString path = folder.filePath(QStringLiteral("alignment.json"));
    for (const QByteArray& content :
         {QByteArray("{ no"), QByteArray(R"({"format_version": 2, "guides": []})"),
          QByteArray(R"({"format_version": 1, "guides": ["a"]})")}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(content);
        file.close();
        QVERIFY(!streetpano::stitch::readAlignment(path).alignment.has_value());
    }
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
