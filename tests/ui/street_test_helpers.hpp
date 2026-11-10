#pragma once

#include <QImage>
#include <QPolygonF>
#include <QString>
#include <QTemporaryDir>
#include <vector>

#include "core/patch.hpp"
#include "stitch/pose_file.hpp"
#include "stitch/stitcher.hpp"
#include "stitch_fixture.hpp"
#include "ui/main_window.hpp"

namespace streetpano::test {

using streetpano::core::WallPoint;
using streetpano::stitch::PoseFile;
using streetpano::stitch::readPoseFile;
using streetpano::stitch::StitchResult;
using streetpano::ui::MainWindow;

inline constexpr int kPhotos = 12;

struct Street {
    QTemporaryDir folder;
    QString posesPath;

    explicit Street(int count = kPhotos) {
        for (int index = 0; index < count; ++index) {
            StreetFixture::writePhoto(folder, index);
        }
        posesPath = StreetFixture::writePoses(folder, StreetFixture::poses(count));
    }

    [[nodiscard]] PoseFile poses() const { return *readPoseFile(posesPath).file; }
};

// A square of panorama pixels near the middle.
inline QPolygonF squareAt(const QImage& panorama, int size = 40) {
    const double x = panorama.width() / 2.0;
    const double y = panorama.height() / 2.0;
    return QPolygonF{QPointF(x, y), QPointF(x + size, y), QPointF(x + size, y + size),
                     QPointF(x, y + size)};
}

inline std::vector<WallPoint> wallSquare(const StitchResult& result, const QPolygonF& polygon) {
    std::vector<WallPoint> wall;
    for (const QPointF& point : polygon) {
        wall.push_back({result.plan.leftArc + point.x() * result.plan.unitsPerPixel,
                        result.plan.topHeight - point.y() * result.plan.unitsPerPixel});
    }
    return wall;
}

inline bool stitchInWindow(MainWindow& window, const Street& street) {
    return window.openFolder(street.folder.path()) && window.startStitch(street.posesPath);
}

}  // namespace streetpano::test
