#pragma once

#include <QColor>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>
#include <cmath>

namespace streetpano::test {

// A small street with a known facade: cameras at (index, 0, 0) looking along +Z at a plane at
// Z = 15, so that the true colour of every point on the facade is known exactly.
struct StreetFixture {
    static constexpr int kWidth = 320;
    static constexpr int kHeight = 180;
    static constexpr double kFocal = 213.0;
    static constexpr double kDepth = 15.0;

    [[nodiscard]] static QRgb facadeColor(double x, double y) {
        const auto channel = [](double value) {
            return static_cast<int>(127.5 + 127.5 * std::sin(value));
        };
        return qRgb(channel(x * 2.1), channel(y * 3.3), channel((x + y) * 1.3));
    }

    static void writePhoto(const QTemporaryDir& folder, int index, double exposure = 1.0) {
        QImage image(kWidth, kHeight, QImage::Format_RGB32);
        for (int v = 0; v < kHeight; ++v) {
            for (int u = 0; u < kWidth; ++u) {
                const double x = index + (u + 0.5 - kWidth / 2.0) * kDepth / kFocal;
                const double y = (v + 0.5 - kHeight / 2.0) * kDepth / kFocal;
                const QRgb color = facadeColor(x, y);
                image.setPixel(u, v,
                               qRgb(qBound(0, static_cast<int>(qRed(color) * exposure), 255),
                                    qBound(0, static_cast<int>(qGreen(color) * exposure), 255),
                                    qBound(0, static_cast<int>(qBlue(color) * exposure), 255)));
            }
        }
        image.save(folder.filePath(QStringLiteral("%1.png").arg(index, 3, 10, QLatin1Char('0'))));
    }

    [[nodiscard]] static QJsonObject poses(int count) {
        QJsonArray poseArray;
        for (int index = 0; index < count; ++index) {
            poseArray.append(QJsonObject{
                {QStringLiteral("name"),
                 QStringLiteral("%1.png").arg(index, 3, 10, QLatin1Char('0'))},
                {QStringLiteral("center"), QJsonArray{index, 0, 0}},
                {QStringLiteral("rotation_world_to_camera"),
                 QJsonArray{QJsonArray{1, 0, 0}, QJsonArray{0, 1, 0}, QJsonArray{0, 0, 1}}},
                {QStringLiteral("up_in_camera"), QJsonArray{0, -1, 0}}});
        }
        QJsonArray pointArray;
        for (int across = 0; across < 40; ++across) {
            for (int down = 0; down < 10; ++down) {
                pointArray.append(QJsonArray{-10.0 + across, -5.0 + down, kDepth});
            }
        }
        return QJsonObject{{QStringLiteral("format_version"), 2},
                           {QStringLiteral("focal_px"), kFocal},
                           {QStringLiteral("poses"), poseArray},
                           {QStringLiteral("points"), pointArray}};
    }

    static QString writePoses(const QTemporaryDir& folder, const QJsonObject& content) {
        const QString path = folder.filePath(QStringLiteral("poses.json"));
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(content).toJson());
        }
        return path;
    }
};

}  // namespace streetpano::test
