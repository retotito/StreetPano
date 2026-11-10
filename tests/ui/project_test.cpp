#include "stitch/project.hpp"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include "ui/recent_projects.hpp"

using streetpano::stitch::openOrCreateProject;
using streetpano::stitch::Project;
using streetpano::stitch::readProject;
using streetpano::stitch::workFolderFor;
using streetpano::stitch::writeProject;
using streetpano::ui::RecentProjects;

class ProjectTest : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase();
    void theWorkFolderSitsBesideThePhotos();
    void aNewProjectIsNamedAfterItsFolder();
    void aNewProjectCanHaveItsOwnName();
    void anExistingProjectIsOpenedAndCanBeRenamed();
    void photosBesideTheProjectSurviveAMove();
    void photosFarAwayAreStoredWhole();
    void aMissingFolderIsRefused();
    void damagedProjectsAreRefused_data();
    void damagedProjectsAreRefused();
    void aProjectNeedsAWritableFolder();
    void recentProjectsAreNewestFirst();
    void recentProjectsThatAreGoneAreLeftOut();
    void recentProjectsAreLimited();
};

namespace {

QString makeFolder(const QTemporaryDir& root, const QString& name) {
    QDir(root.path()).mkpath(name);
    return QDir(root.path()).filePath(name);
}

QString writeFile(const QString& path, const QByteArray& content) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(content);
    }
    return path;
}

}  // namespace

void ProjectTest::initTestCase() {
    // Keeps the recent projects list out of the real settings.
    QStandardPaths::setTestModeEnabled(true);
}

void ProjectTest::theWorkFolderSitsBesideThePhotos() {
    const QTemporaryDir root;
    const QString photos = makeFolder(root, QStringLiteral("street"));
    QCOMPARE(workFolderFor(photos), photos + QStringLiteral(".streetpano"));
    QCOMPARE(workFolderFor(photos + QStringLiteral("/")), photos + QStringLiteral(".streetpano"));
}

void ProjectTest::aNewProjectIsNamedAfterItsFolder() {
    const QTemporaryDir root;
    const QString photos = makeFolder(root, QStringLiteral("Bahnhofstrasse"));
    const auto result = openOrCreateProject(photos, QStringLiteral("  "));
    QVERIFY(result.project.has_value());
    QCOMPARE(result.project->name, QStringLiteral("Bahnhofstrasse"));
    QCOMPARE(result.project->photoFolder, photos);
    QVERIFY(QFile::exists(result.project->projectFile()));
    QCOMPARE(result.project->posesFile(),
             QDir(workFolderFor(photos)).filePath(QStringLiteral("poses.json")));
    QCOMPARE(result.project->panoramaFile(),
             QDir(workFolderFor(photos)).filePath(QStringLiteral("panorama.jpg")));
    // Nothing is written into the photo folder itself.
    QVERIFY(QDir(photos).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
}

void ProjectTest::aNewProjectCanHaveItsOwnName() {
    const QTemporaryDir root;
    const QString photos = makeFolder(root, QStringLiteral("Photos-1-001-jpeg"));
    const auto result = openOrCreateProject(photos, QStringLiteral(" Müllackerstrasse "));
    QCOMPARE(result.project->name, QStringLiteral("Müllackerstrasse"));
    QCOMPARE(readProject(result.project->projectFile()).project->name,
             QStringLiteral("Müllackerstrasse"));
}

void ProjectTest::anExistingProjectIsOpenedAndCanBeRenamed() {
    const QTemporaryDir root;
    const QString photos = makeFolder(root, QStringLiteral("street"));
    QVERIFY(openOrCreateProject(photos, QStringLiteral("First")).project.has_value());
    QCOMPARE(openOrCreateProject(photos, {}).project->name, QStringLiteral("First"));
    QCOMPARE(openOrCreateProject(photos, QStringLiteral("Second")).project->name,
             QStringLiteral("Second"));
    QCOMPARE(openOrCreateProject(photos, {}).project->name, QStringLiteral("Second"));
}

void ProjectTest::photosBesideTheProjectSurviveAMove() {
    const QTemporaryDir root;
    const QString photos = makeFolder(root, QStringLiteral("street"));
    QVERIFY(openOrCreateProject(photos, {}).project.has_value());
    const QString moved = makeFolder(root, QStringLiteral("moved"));
    QVERIFY(QDir().rename(photos, moved + QStringLiteral("/street")));
    QVERIFY(QDir().rename(photos + QStringLiteral(".streetpano"),
                          moved + QStringLiteral("/street.streetpano")));
    const auto result = readProject(moved + QStringLiteral("/street.streetpano/project.json"));
    QVERIFY(result.project.has_value());
    QCOMPARE(result.project->photoFolder, moved + QStringLiteral("/street"));
}

void ProjectTest::photosFarAwayAreStoredWhole() {
    const QTemporaryDir root;
    const QString photos = makeFolder(root, QStringLiteral("a/b/street"));
    Project project;
    project.name = QStringLiteral("Far");
    project.photoFolder = photos;
    project.workFolder = makeFolder(root, QStringLiteral("elsewhere/deep/work"));
    QVERIFY(writeProject(project));
    QFile file(project.projectFile());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.readAll().contains(photos.toUtf8()));
    QCOMPARE(readProject(project.projectFile()).project->photoFolder, photos);
}

void ProjectTest::aMissingFolderIsRefused() {
    const QTemporaryDir root;
    const auto result = openOrCreateProject(root.filePath(QStringLiteral("none")), {});
    QVERIFY(!result.project.has_value());
    QVERIFY(result.error.contains(QStringLiteral("isn't a folder")));
}

void ProjectTest::damagedProjectsAreRefused_data() {
    QTest::addColumn<QByteArray>("content");
    QTest::newRow("not JSON") << QByteArray("{ nope");
    QTest::newRow("another version")
        << QByteArray(R"({"format_version": 9, "name": "a", "photos": "b"})");
    QTest::newRow("no name") << QByteArray(R"({"format_version": 1, "photos": "b"})");
    QTest::newRow("no photos") << QByteArray(R"({"format_version": 1, "name": "a"})");
}

void ProjectTest::damagedProjectsAreRefused() {
    QFETCH(QByteArray, content);
    const QTemporaryDir root;
    const auto result =
        readProject(writeFile(root.filePath(QStringLiteral("p/project.json")), content));
    QVERIFY(!result.project.has_value());
    QVERIFY(result.error.contains(QStringLiteral("isn't a valid project")));
    QVERIFY(!readProject(root.filePath(QStringLiteral("missing.json"))).project.has_value());
    const QByteArray big(2 * 1024 * 1024, ' ');
    QVERIFY(!readProject(writeFile(root.filePath(QStringLiteral("big/project.json")), big))
                 .project.has_value());
}

void ProjectTest::aProjectNeedsAWritableFolder() {
    const QTemporaryDir root;
    // A file where the work folder should go stops the project from being created.
    const QString photos = makeFolder(root, QStringLiteral("street"));
    writeFile(photos + QStringLiteral(".streetpano"), "in the way");
    const auto result = openOrCreateProject(photos, {});
    QVERIFY(!result.project.has_value());
    QVERIFY(result.error.contains(QStringLiteral("Can't create")));
}

void ProjectTest::recentProjectsAreNewestFirst() {
    const QTemporaryDir root;
    const QString a = writeFile(root.filePath(QStringLiteral("a/project.json")), "{}");
    const QString b = writeFile(root.filePath(QStringLiteral("b/project.json")), "{}");
    RecentProjects::add(a);
    RecentProjects::add(b);
    QCOMPARE(RecentProjects::load().mid(0, 2), (QStringList{b, a}));
    RecentProjects::add(a);
    QCOMPARE(RecentProjects::load().mid(0, 2), (QStringList{a, b}));
    RecentProjects::remove(a);
    QVERIFY(!RecentProjects::load().contains(a));
}

void ProjectTest::recentProjectsThatAreGoneAreLeftOut() {
    QString gone;
    {
        const QTemporaryDir root;
        gone = writeFile(root.filePath(QStringLiteral("c/project.json")), "{}");
        RecentProjects::add(gone);
        QVERIFY(RecentProjects::load().contains(gone));
    }
    QVERIFY(!RecentProjects::load().contains(gone));
}

void ProjectTest::recentProjectsAreLimited() {
    const QTemporaryDir root;
    for (int index = 0; index < RecentProjects::kMaxProjects + 5; ++index) {
        RecentProjects::add(
            writeFile(root.filePath(QStringLiteral("%1/project.json").arg(index)), "{}"));
    }
    QCOMPARE(RecentProjects::load().size(), RecentProjects::kMaxProjects);
}

QTEST_MAIN(ProjectTest)
#include "project_test.moc"
