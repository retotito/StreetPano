#include "stitch/stitcher.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>
#include <cmath>

#include "core/stitch_plan.hpp"
#include "stitch/pose_file.hpp"
#include "stitch_fixture.hpp"
#include "test_helpers.hpp"
#include "ui/main_window.hpp"
#include "ui/stitch_job.hpp"

using streetpano::core::StitchOptions;
using streetpano::stitch::PoseFile;
using streetpano::stitch::readPoseFile;
using streetpano::stitch::stitchPanorama;
using streetpano::test::StreetFixture;
using streetpano::ui::MainWindow;
using streetpano::ui::StitchJob;

class StitcherTest : public QObject {
    Q_OBJECT

  private slots:
    void matchesTheTrueFacade();
    void matchesTheTrueFacadeWhenReduced();
    void hidesExposureDifferencesAtSeams();
    void reportsProgress();
    void respectsTheMemoryBudget();
    void canBeCancelled();
    void skipsMissingAndDamagedPhotos();
    void skipsPhotosOfADifferentSize();
    void failsClearlyWithoutPhotos();
    void failsClearlyWithoutPoints();
    void jobDeliversAResult();
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
    const auto frame = *streetpano::core::fitStreetFrame(poses.poses, poses.points).frame;
    const auto plan =
        *streetpano::core::makeStitchPlan(frame, poses.poses, poses.focalPixels,
                                          StreetFixture::kWidth, StreetFixture::kHeight, options)
             .plan;
    double total = 0.0;
    int count = 0;
    for (int y = 4; y < panorama.height() - 4; ++y) {
        for (int x = 4; x < panorama.width() - 4; ++x) {
            const double u = plan.leftOnPlane + (x + 0.5) * plan.unitsPerPixel;
            const double v = plan.topOnPlane - (y + 0.5) * plan.unitsPerPixel;
            const auto point = frame.origin + frame.right * u + frame.up * v;
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
    QTemporaryDir folder;
    for (int index = 0; index < 4; ++index) {
        StreetFixture::writePhoto(folder, index, index % 2 == 0 ? 0.7 : 1.0);
    }
    const auto poses =
        *readPoseFile(StreetFixture::writePoses(folder, StreetFixture::poses(4))).file;
    StitchOptions feathered;
    feathered.featherPixels = 8;
    StitchOptions hard;
    hard.featherPixels = 0;
    const auto soft = stitchPanorama(poses, QDir(folder.path()), feathered);
    const auto sharp = stitchPanorama(poses, QDir(folder.path()), hard);

    const auto largestStep = [](const QImage& image) {
        int largest = 0;
        const int y = image.height() / 2;
        for (int x = 1; x < image.width(); ++x) {
            largest = qMax(largest, qAbs(qGray(image.pixel(x, y)) - qGray(image.pixel(x - 1, y))));
        }
        return largest;
    };
    QVERIFY(largestStep(soft.panorama) < largestStep(sharp.panorama));
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
