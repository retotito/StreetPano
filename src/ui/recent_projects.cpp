#include "ui/recent_projects.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace streetpano::ui {
namespace {

const char* const kKey = "recentProjects";

[[nodiscard]] QString settingsFile() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath(QStringLiteral("settings.ini"));
}

}  // namespace

QStringList RecentProjects::load() {
    const QSettings settings(settingsFile(), QSettings::IniFormat);
    QStringList files;
    for (const QString& file : settings.value(QString::fromLatin1(kKey)).toStringList()) {
        if (QFile::exists(file)) {
            files.append(file);
        }
    }
    return files;
}

void RecentProjects::add(const QString& projectFile) {
    QStringList files = load();
    files.removeAll(projectFile);
    files.prepend(projectFile);
    while (files.size() > kMaxProjects) {
        files.removeLast();
    }
    save(files);
}

void RecentProjects::remove(const QString& projectFile) {
    QStringList files = load();
    files.removeAll(projectFile);
    save(files);
}

void RecentProjects::save(const QStringList& files) {
    QDir().mkpath(QFileInfo(settingsFile()).absolutePath());
    QSettings settings(settingsFile(), QSettings::IniFormat);
    settings.setValue(QString::fromLatin1(kKey), files);
}

}  // namespace streetpano::ui
