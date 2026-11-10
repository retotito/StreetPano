#include "stitch/project.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace streetpano::stitch {
namespace {

constexpr int kFormatVersion = 1;
constexpr qint64 kMaxFileBytes = qint64{1024} * 1024;

}  // namespace

QString Project::projectFile() const {
    return QDir(workFolder).filePath(QStringLiteral("project.json"));
}
QString Project::posesFile() const {
    return QDir(workFolder).filePath(QStringLiteral("poses.json"));
}
QString Project::panoramaFile() const {
    return QDir(workFolder).filePath(QStringLiteral("panorama.jpg"));
}

QString workFolderFor(const QString& photoFolder) {
    return QDir::cleanPath(QFileInfo(photoFolder).absoluteFilePath()) +
           QStringLiteral(".streetpano");
}

ProjectResult readProject(const QString& projectFile) {
    ProjectResult result;
    QFile file(projectFile);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("Can't open %1: %2").arg(projectFile, file.errorString());
        return result;
    }
    if (file.size() > kMaxFileBytes) {
        result.error = QStringLiteral("%1 is too large to be a project.").arg(projectFile);
        return result;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const QJsonObject root = document.object();
    if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
        root.value(QStringLiteral("format_version")).toInt() != kFormatVersion) {
        result.error = QStringLiteral("%1 isn't a valid project.").arg(projectFile);
        return result;
    }
    Project project;
    project.workFolder = QFileInfo(projectFile).absolutePath();
    project.name = root.value(QStringLiteral("name")).toString();
    const QString photos = root.value(QStringLiteral("photos")).toString();
    if (project.name.isEmpty() || photos.isEmpty()) {
        result.error = QStringLiteral("%1 isn't a valid project.").arg(projectFile);
        return result;
    }
    project.photoFolder = QDir::cleanPath(QDir(project.workFolder).absoluteFilePath(photos));
    result.project = project;
    return result;
}

bool writeProject(const Project& project) {
    if (!QDir().mkpath(project.workFolder)) {
        return false;
    }
    const QDir work(project.workFolder);
    const QString relative = work.relativeFilePath(project.photoFolder);
    // A path that climbs more than one folder is unlikely to survive a move, so it's kept whole.
    const bool beside =
        !relative.startsWith(QStringLiteral("../../")) && !QDir::isAbsolutePath(relative);
    const QJsonObject root{{QStringLiteral("format_version"), kFormatVersion},
                           {QStringLiteral("name"), project.name},
                           {QStringLiteral("photos"), beside ? relative : project.photoFolder}};
    QSaveFile file(project.projectFile());
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson());
    return file.commit();
}

ProjectResult openOrCreateProject(const QString& photoFolder, const QString& name) {
    ProjectResult result;
    if (!QFileInfo(photoFolder).isDir()) {
        result.error = QStringLiteral("%1 isn't a folder.").arg(photoFolder);
        return result;
    }
    Project project;
    project.photoFolder = QDir::cleanPath(QFileInfo(photoFolder).absoluteFilePath());
    project.workFolder = workFolderFor(project.photoFolder);
    if (QFile::exists(project.projectFile())) {
        result = readProject(project.projectFile());
        if (result.project.has_value() && !name.trimmed().isEmpty() &&
            result.project->name != name.trimmed()) {
            result.project->name = name.trimmed();
            if (!writeProject(*result.project)) {
                result.error = QStringLiteral("Can't rename the project in %1.")
                                   .arg(result.project->projectFile());
                result.project.reset();
            }
        }
        return result;
    }
    project.name = name.trimmed().isEmpty() ? QDir(project.photoFolder).dirName() : name.trimmed();
    if (!writeProject(project)) {
        result.error = QStringLiteral("Can't create a project in %1.").arg(project.workFolder);
        return result;
    }
    result.project = project;
    return result;
}

}  // namespace streetpano::stitch
