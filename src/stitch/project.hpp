#pragma once

#include <QString>
#include <optional>

namespace streetpano::stitch {

// A street project. Photos stay where they are, and are never copied or changed. The project's
// own files go in a work folder, which by default sits beside the photos.
struct Project {
    QString name;
    QString photoFolder;
    QString workFolder;

    [[nodiscard]] QString projectFile() const;
    [[nodiscard]] QString posesFile() const;
    [[nodiscard]] QString panoramaFile() const;
};

struct ProjectResult {
    std::optional<Project> project;
    QString error;
};

// The default work folder for a folder of photos: the same name plus ".streetpano".
[[nodiscard]] QString workFolderFor(const QString& photoFolder);

[[nodiscard]] ProjectResult readProject(const QString& projectFile);

// Writes project.json, creating the work folder if needed. The photo folder is stored relative to
// the work folder when it's beside it, so the two can be moved together.
[[nodiscard]] bool writeProject(const Project& project);

// Opens the project for this folder of photos, or creates one with this name. An empty name uses
// the folder's name.
[[nodiscard]] ProjectResult openOrCreateProject(const QString& photoFolder, const QString& name);

}  // namespace streetpano::stitch
