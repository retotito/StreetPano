#pragma once

#include <QColor>
#include <QImage>
#include <QString>
#include <QTemporaryDir>

namespace streetpano::test {

inline QString writePhoto(const QTemporaryDir& folder, const QString& name, const QColor& color,
                          int width = 320, int height = 180) {
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(color);
    const QString path = folder.filePath(name);
    image.save(path);
    return path;
}

}  // namespace streetpano::test
