#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QStringList>

#include "ui/main_window.hpp"

int main(int argc, char* argv[]) {
    const QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("StreetPano"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Build long street panoramas from photos."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("folder"),
                                 QStringLiteral("Folder of photos to open."),
                                 QStringLiteral("[folder]"));
    const QCommandLineOption stitchOption(QStringLiteral("stitch"),
                                          QStringLiteral("Stitch the folder after opening it."));
    parser.addOption(stitchOption);
    parser.process(application);

    streetpano::ui::MainWindow window;
    window.show();
    const QStringList arguments = parser.positionalArguments();
    if (!arguments.isEmpty()) {
        // Opening a folder makes it a project, so that it appears in the projects list.
        if (window.openProjectForFolder(arguments.first()) && parser.isSet(stitchOption)) {
            window.startStitchFromFolder();
        }
    }
    return QApplication::exec();
}
