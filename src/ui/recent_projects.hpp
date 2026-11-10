#pragma once

#include <QString>
#include <QStringList>

namespace streetpano::ui {

// The project files opened most recently, newest first. They're kept in the app's settings, so
// they survive a restart. Projects that no longer exist are left out.
class RecentProjects {
  public:
    static constexpr qsizetype kMaxProjects = 20;

    [[nodiscard]] static QStringList load();
    static void add(const QString& projectFile);
    static void remove(const QString& projectFile);

  private:
    static void save(const QStringList& files);
};

}  // namespace streetpano::ui
