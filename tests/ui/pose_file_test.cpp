#include "stitch/pose_file.hpp"

#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>

#include "stitch_fixture.hpp"

using streetpano::stitch::readPoseFile;
using streetpano::test::StreetFixture;

class PoseFileTest : public QObject {
    Q_OBJECT

  private slots:
    void readsAValidFile();
    void acceptsAMissingUpDirection();
    void rejectsAMissingFile();
    void rejectsDamagedText_data();
    void rejectsDamagedText();
    void rejectsBadContent_data();
    void rejectsBadContent();
};

namespace {

QJsonObject withPose(const QJsonObject& root, const QString& key, const QJsonValue& value) {
    QJsonArray poses = root.value(QStringLiteral("poses")).toArray();
    QJsonObject first = poses.at(0).toObject();
    first.insert(key, value);
    poses.replace(0, first);
    QJsonObject result = root;
    result.insert(QStringLiteral("poses"), poses);
    return result;
}

}  // namespace

void PoseFileTest::readsAValidFile() {
    const QTemporaryDir folder;
    const auto result = readPoseFile(StreetFixture::writePoses(folder, StreetFixture::poses(3)));
    QVERIFY(result.file.has_value());
    QCOMPARE(result.file->poses.size(), std::size_t{3});
    QCOMPARE(result.file->points.size(), std::size_t{400});
    QCOMPARE(result.file->focalPixels, StreetFixture::kFocal);
    QCOMPARE(result.file->poses[2].center.x, 2.0);
    QCOMPARE(result.file->poses[0].name, std::string("000.png"));
}

void PoseFileTest::acceptsAMissingUpDirection() {
    const QTemporaryDir folder;
    QJsonObject root = StreetFixture::poses(1);
    QJsonArray poses = root.value(QStringLiteral("poses")).toArray();
    QJsonObject first = poses.at(0).toObject();
    first.remove(QStringLiteral("up_in_camera"));
    poses.replace(0, first);
    root.insert(QStringLiteral("poses"), poses);
    const auto result = readPoseFile(StreetFixture::writePoses(folder, root));
    QVERIFY(result.file.has_value());
    QCOMPARE(result.file->poses[0].upInCamera.y, -1.0);
}

void PoseFileTest::rejectsAMissingFile() {
    const QTemporaryDir folder;
    const auto result = readPoseFile(folder.filePath(QStringLiteral("missing.json")));
    QVERIFY(!result.file.has_value());
    QVERIFY(result.error.contains(QStringLiteral("Can't open")));
}

void PoseFileTest::rejectsDamagedText_data() {
    QTest::addColumn<QByteArray>("content");
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("text") << QByteArray("not json");
    QTest::newRow("array") << QByteArray("[1, 2, 3]");
    QTest::newRow("cut off") << QByteArray(R"({"format_version": 2, "poses": [)");
    QTest::newRow("binary") << QByteArray("\x00\xff\x13\x37", 4);
}

void PoseFileTest::rejectsDamagedText() {
    QFETCH(QByteArray, content);
    const QTemporaryDir folder;
    QFile file(folder.filePath(QStringLiteral("poses.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
    file.close();
    const auto result = readPoseFile(file.fileName());
    QVERIFY(!result.file.has_value());
    QVERIFY(!result.error.isEmpty());
}

void PoseFileTest::rejectsBadContent_data() {
    QTest::addColumn<QJsonObject>("content");
    const QJsonObject good = StreetFixture::poses(2);
    const auto with = [&](const QString& key, const QJsonValue& value) {
        QJsonObject result = good;
        result.insert(key, value);
        return result;
    };
    QTest::newRow("old version") << with(QStringLiteral("format_version"), 1);
    QTest::newRow("no focal length") << with(QStringLiteral("focal_px"), 0);
    QTest::newRow("negative focal length") << with(QStringLiteral("focal_px"), -5);
    QTest::newRow("no poses") << with(QStringLiteral("poses"), QJsonArray{});
    QTest::newRow("bad point") << with(QStringLiteral("points"), QJsonArray{QJsonArray{1, 2}});
    QTest::newRow("text point") << with(QStringLiteral("points"),
                                        QJsonArray{QJsonArray{1, QStringLiteral("x"), 3}});

    QTest::newRow("no name") << withPose(good, QStringLiteral("name"), QString());
    QTest::newRow("path outside the folder")
        << withPose(good, QStringLiteral("name"), QStringLiteral("../../secret.png"));
    QTest::newRow("windows path") << withPose(good, QStringLiteral("name"),
                                              QStringLiteral("..\\secret.png"));
    QTest::newRow("bad center") << withPose(good, QStringLiteral("center"), QJsonArray{1, 2});
    QTest::newRow("short rotation") << withPose(good, QStringLiteral("rotation_world_to_camera"),
                                                QJsonArray{QJsonArray{1, 0, 0}});
    QTest::newRow("bad rotation row")
        << withPose(good, QStringLiteral("rotation_world_to_camera"),
                    QJsonArray{QJsonArray{1, 0}, QJsonArray{0, 1, 0}, QJsonArray{0, 0, 1}});
    QTest::newRow("scaled rotation")
        << withPose(good, QStringLiteral("rotation_world_to_camera"),
                    QJsonArray{QJsonArray{2, 0, 0}, QJsonArray{0, 1, 0}, QJsonArray{0, 0, 1}});
    QTest::newRow("skewed rotation")
        << withPose(good, QStringLiteral("rotation_world_to_camera"),
                    QJsonArray{QJsonArray{1, 0, 0}, QJsonArray{1, 0, 0}, QJsonArray{0, 0, 1}});
    QTest::newRow("mirrored rotation")
        << withPose(good, QStringLiteral("rotation_world_to_camera"),
                    QJsonArray{QJsonArray{-1, 0, 0}, QJsonArray{0, 1, 0}, QJsonArray{0, 0, 1}});
    QTest::newRow("zero up") << withPose(good, QStringLiteral("up_in_camera"), QJsonArray{0, 0, 0});
    QTest::newRow("bad up") << withPose(good, QStringLiteral("up_in_camera"), QJsonArray{0});
}

void PoseFileTest::rejectsBadContent() {
    QFETCH(QJsonObject, content);
    const QTemporaryDir folder;
    const auto result = readPoseFile(StreetFixture::writePoses(folder, content));
    QVERIFY(!result.file.has_value());
    QVERIFY(!result.error.isEmpty());
}

QTEST_GUILESS_MAIN(PoseFileTest)
#include "pose_file_test.moc"
