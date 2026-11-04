// Developer tool: opens a folder without a screen and saves a picture of the window.
// Usage: app_screenshot <folder> <output.png> [photo number]

#include <QApplication>
#include <QTest>
#include <QTimer>

#include "ui/main_window.hpp"

int main(int argc, char* argv[]) {
    const QApplication application(argc, argv);
    const QStringList arguments = QApplication::arguments();
    if (arguments.size() < 3) {
        return 2;
    }
    streetpano::ui::MainWindow window;
    window.resize(1280, 800);
    window.show();
    if (!window.openFolder(arguments.at(1))) {
        return 1;
    }
    if (arguments.size() > 3) {
        window.selectPhoto(arguments.at(3).toInt());
    }
    QTest::qWait(2500);
    return window.grab().save(arguments.at(2)) ? 0 : 1;
}
