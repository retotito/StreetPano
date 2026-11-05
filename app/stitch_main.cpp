// Builds a panorama from the command line, without opening a window.

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QTextStream>
#include <cstdio>
#include <exception>

#include "stitch/decisions.hpp"
#include "stitch/pose_file.hpp"
#include "stitch/stitcher.hpp"

namespace {

int run(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    const QGuiApplication application(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Build a panorama from photos and poses."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("poses"), QStringLiteral("The poses.json file."));
    parser.addPositionalArgument(QStringLiteral("photos"), QStringLiteral("Folder of photos."));
    parser.addPositionalArgument(QStringLiteral("output"), QStringLiteral("Image file to write."));
    parser.process(application);
    const QStringList arguments = parser.positionalArguments();
    if (arguments.size() != 3) {
        parser.showHelp(2);
    }

    const streetpano::stitch::PoseFileResult poses =
        streetpano::stitch::readPoseFile(arguments.at(0));
    if (!poses.file.has_value()) {
        err << poses.error << Qt::endl;
        return 1;
    }
    const streetpano::stitch::StitchResult result = streetpano::stitch::stitchPanorama(
        *poses.file, QDir(arguments.at(1)), {},
        [&out](int done, int total) { out << "\rPhoto " << done << " of " << total << Qt::flush; });
    out << Qt::endl;
    for (const QString& warning : result.warnings) {
        err << warning << Qt::endl;
    }
    if (result.panorama.isNull()) {
        err << result.error << Qt::endl;
        return 1;
    }
    const auto decisions =
        streetpano::stitch::readDecisions(streetpano::stitch::decisionsPathFor(arguments.at(0)));
    if (!decisions.decisions.has_value()) {
        err << decisions.error << Qt::endl;
        return 1;
    }
    QStringList patchWarnings;
    const QImage finished = streetpano::stitch::withPatches(
        result, QDir(arguments.at(1)), decisions.decisions->patches, patchWarnings);
    for (const QString& warning : patchWarnings) {
        err << warning << Qt::endl;
    }
    if (!streetpano::stitch::savePanorama(finished, arguments.at(2))) {
        err << "Couldn't write " << arguments.at(2) << Qt::endl;
        return 1;
    }
    out << "Aligned the wall at " << result.alignment.moved << " of " << result.alignment.seams
        << " seams" << Qt::endl;
    out << "Wrote " << result.panorama.width() << " x " << result.panorama.height() << " px at "
        << qRound(result.scale * 100.0) << "% of full detail to " << arguments.at(2) << Qt::endl;
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::fputs(error.what(), stderr);
        return 1;
    }
}
