#include "stitch/decisions.hpp"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

using streetpano::core::Patch;
using streetpano::stitch::Decisions;
using streetpano::stitch::decisionsPathFor;
using streetpano::stitch::readDecisions;
using streetpano::stitch::savePanorama;
using streetpano::stitch::writeDecisions;

class DecisionsTest : public QObject {
    Q_OBJECT

  private slots:
    void keepsDecisionsBesideTheCameraPositions();
    void aMissingFileMeansNoEditsYet();
    void writtenDecisionsReadBackTheSame();
    void anAreaCanHoldJustADescription();
    void rejectsADamagedFile();
    void rejectsADifferentVersion();
    void rejectsBadEdits_data();
    void rejectsBadEdits();
    void aFailedWriteLeavesTheOldFile();
    void savesAPanorama();
    void aFailedSaveLeavesTheOldPanorama();
};

namespace {

QString write(const QTemporaryDir& folder, const QByteArray& content) {
    const QString path = folder.filePath(QStringLiteral("decisions.json"));
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(content);
    }
    return path;
}

Decisions onePatch() {
    Decisions decisions;
    decisions.patches.push_back(Patch{
        {{1.5, 2.5}, {3.5, 2.5}, {3.5, -1.0}}, "photo 1.jpg", "fix the sign", "remove", true});
    return decisions;
}

}  // namespace

void DecisionsTest::keepsDecisionsBesideTheCameraPositions() {
    const QTemporaryDir folder;
    QCOMPARE(decisionsPathFor(folder.filePath(QStringLiteral("poses.json"))),
             folder.filePath(QStringLiteral("decisions.json")));
}

void DecisionsTest::aMissingFileMeansNoEditsYet() {
    const QTemporaryDir folder;
    const auto result = readDecisions(folder.filePath(QStringLiteral("none.json")));
    QVERIFY(result.decisions.has_value());
    QVERIFY(result.decisions->patches.empty());
}

void DecisionsTest::writtenDecisionsReadBackTheSame() {
    const QTemporaryDir folder;
    const QString path = folder.filePath(QStringLiteral("decisions.json"));
    QVERIFY(writeDecisions(onePatch(), path));
    const auto result = readDecisions(path);
    QVERIFY(result.decisions.has_value());
    QCOMPARE(result.decisions->patches.size(), std::size_t{1});
    const Patch& patch = result.decisions->patches[0];
    QCOMPARE(patch.photo, std::string("photo 1.jpg"));
    QCOMPARE(patch.outline.size(), std::size_t{3});
    QCOMPARE(patch.outline[2].arc, 3.5);
    QCOMPARE(patch.outline[2].height, -1.0);
    QCOMPARE(patch.note, std::string("fix the sign"));
    QCOMPARE(patch.kind, std::string("remove"));
    QVERIFY(patch.hidden);
}

void DecisionsTest::anAreaCanHoldJustADescription() {
    const QTemporaryDir folder;
    const auto result = readDecisions(write(
        folder,
        R"({"format_version": 1, "patches": [{"note": "remove the van", "outline": [[0, 0], [1, 0], [1, 1]]}]})"));
    QVERIFY(result.decisions.has_value());
    QVERIFY(result.decisions->patches[0].photo.empty());
    QCOMPARE(result.decisions->patches[0].note, std::string("remove the van"));
}

void DecisionsTest::rejectsADamagedFile() {
    const QTemporaryDir folder;
    const auto result = readDecisions(write(folder, "{ not json"));
    QVERIFY(!result.decisions.has_value());
    QVERIFY(result.error.contains(QStringLiteral("isn't a valid")));
}

void DecisionsTest::rejectsADifferentVersion() {
    const QTemporaryDir folder;
    const auto result = readDecisions(write(folder, R"({"format_version": 99, "patches": []})"));
    QVERIFY(!result.decisions.has_value());
    QVERIFY(result.error.contains(QStringLiteral("version")));
}

void DecisionsTest::rejectsBadEdits_data() {
    QTest::addColumn<QByteArray>("patch");
    const QByteArray good = R"("outline": [[0, 0], [1, 0], [1, 1]])";
    QTest::newRow("no photo") << R"({)" + good + R"(})";
    QTest::newRow("a path, not a file name") << R"({"photo": "../secret.jpg", )" + good + R"(})";
    QTest::newRow("a Windows path") << R"({"photo": "a\\b.jpg", )" + good + R"(})";
    QTest::newRow("too few points")
        << QByteArray(R"({"photo": "a.jpg", "outline": [[0, 0], [1, 1]]})");
    QTest::newRow("a point without two numbers")
        << QByteArray(R"({"photo": "a.jpg", "outline": [[0, 0], [1], [1, 1]]})");
    QTest::newRow("a point that isn't numbers")
        << QByteArray(R"({"photo": "a.jpg", "outline": [[0, 0], ["1", 0], [1, 1]]})");
}

void DecisionsTest::rejectsBadEdits() {
    QFETCH(QByteArray, patch);
    const QTemporaryDir folder;
    const auto result =
        readDecisions(write(folder, R"({"format_version": 1, "patches": [)" + patch + "]}"));
    QVERIFY(!result.decisions.has_value());
    QVERIFY(result.error.contains(QStringLiteral("invalid edit")));
}

void DecisionsTest::aFailedWriteLeavesTheOldFile() {
    const QTemporaryDir folder;
    QVERIFY(!writeDecisions(onePatch(), folder.filePath(QStringLiteral("no/such/folder/d.json"))));
}

void DecisionsTest::savesAPanorama() {
    const QTemporaryDir folder;
    const QString path = folder.filePath(QStringLiteral("panorama.jpg"));
    QImage image(64, 32, QImage::Format_ARGB32);
    image.fill(qRgb(40, 80, 120));
    QVERIFY(savePanorama(image, path));
    QCOMPARE(QImage(path).size(), image.size());
    QVERIFY(QDir(folder.path()).entryList(QDir::Files).size() == 1);  // No temporary file left.
}

void DecisionsTest::aFailedSaveLeavesTheOldPanorama() {
    const QTemporaryDir folder;
    const QString path = folder.filePath(QStringLiteral("panorama.not-a-format"));
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("the old panorama");
    }
    QImage image(64, 32, QImage::Format_ARGB32);
    image.fill(qRgb(40, 80, 120));
    QVERIFY(!savePanorama(image, path));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("the old panorama"));
    QVERIFY(!savePanorama(image, folder.filePath(QStringLiteral("no/such/folder/p.jpg"))));
}

QTEST_MAIN(DecisionsTest)
#include "decisions_test.moc"
