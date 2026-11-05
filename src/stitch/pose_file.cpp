#include "stitch/pose_file.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

namespace streetpano::stitch {
namespace {

constexpr int kFormatVersion = 2;
constexpr qint64 kMaxFileBytes = qint64{512} * 1024 * 1024;
constexpr double kRotationTolerance = 1e-3;

[[nodiscard]] std::optional<core::Vec3> readVector(const QJsonValue& value) {
    const QJsonArray array = value.toArray();
    if (array.size() != 3 || !array.at(0).isDouble() || !array.at(1).isDouble() ||
        !array.at(2).isDouble()) {
        return std::nullopt;
    }
    const core::Vec3 vector{array.at(0).toDouble(), array.at(1).toDouble(), array.at(2).toDouble()};
    if (!core::isFinite(vector)) {
        return std::nullopt;
    }
    return vector;
}

[[nodiscard]] bool isRotation(const core::Mat3& m) {
    const auto near = [](double value, double expected) {
        return std::abs(value - expected) < kRotationTolerance;
    };
    return near(core::dot(m[0], m[0]), 1.0) && near(core::dot(m[1], m[1]), 1.0) &&
           near(core::dot(m[2], m[2]), 1.0) && near(core::dot(m[0], m[1]), 0.0) &&
           near(core::dot(m[0], m[2]), 0.0) && near(core::dot(m[1], m[2]), 0.0) &&
           near(core::dot(core::cross(m[0], m[1]), m[2]), 1.0);
}

[[nodiscard]] std::optional<core::CameraPose> readPose(const QJsonObject& object) {
    const QString name = object.value(QStringLiteral("name")).toString();
    const auto center = readVector(object.value(QStringLiteral("center")));
    const QJsonArray rows = object.value(QStringLiteral("rotation_world_to_camera")).toArray();
    // Only a file name is accepted, so that a damaged file can't point outside the folder.
    if (name.isEmpty() || name.contains(u'/') || name.contains(u'\\') || !center.has_value() ||
        rows.size() != 3) {
        return std::nullopt;
    }
    const auto row0 = readVector(rows.at(0));
    const auto row1 = readVector(rows.at(1));
    const auto row2 = readVector(rows.at(2));
    if (!row0.has_value() || !row1.has_value() || !row2.has_value()) {
        return std::nullopt;
    }
    core::CameraPose pose;
    pose.name = name.toStdString();
    pose.center = *center;
    pose.worldToCamera = {*row0, *row1, *row2};
    if (!isRotation(pose.worldToCamera)) {
        return std::nullopt;
    }
    const QJsonValue up = object.value(QStringLiteral("up_in_camera"));
    if (!up.isUndefined()) {
        const auto upVector = readVector(up);
        if (!upVector.has_value() || core::length(*upVector) == 0.0) {
            return std::nullopt;
        }
        pose.upInCamera = *upVector;
    }
    return pose;
}

}  // namespace

PoseFileResult readPoseFile(const QString& path) {
    PoseFileResult result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("Can't open %1: %2").arg(path, file.errorString());
        return result;
    }
    if (file.size() > kMaxFileBytes) {
        result.error = QStringLiteral("%1 is too large to be a camera position file.").arg(path);
        return result;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("%1 isn't a valid camera position file.").arg(path);
        return result;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format_version")).toInt() != kFormatVersion) {
        result.error =
            QStringLiteral("%1 uses a different format version. Please create it again.").arg(path);
        return result;
    }

    PoseFile poses;
    poses.focalPixels = root.value(QStringLiteral("focal_px")).toDouble();
    if (!std::isfinite(poses.focalPixels) || poses.focalPixels <= 0.0) {
        result.error = QStringLiteral("%1 has an invalid focal length.").arg(path);
        return result;
    }
    const QJsonArray poseArray = root.value(QStringLiteral("poses")).toArray();
    poses.poses.reserve(static_cast<std::size_t>(poseArray.size()));
    for (qsizetype index = 0; index < poseArray.size(); ++index) {
        std::optional<core::CameraPose> pose = readPose(poseArray.at(index).toObject());
        if (!pose.has_value()) {
            result.error = QStringLiteral("%1 contains an invalid camera position.").arg(path);
            return result;
        }
        poses.poses.push_back(std::move(*pose));
    }
    const QJsonArray pointArray = root.value(QStringLiteral("points")).toArray();
    poses.points.reserve(static_cast<std::size_t>(pointArray.size()));
    for (qsizetype index = 0; index < pointArray.size(); ++index) {
        const auto point = readVector(pointArray.at(index));
        if (!point.has_value()) {
            result.error = QStringLiteral("%1 contains an invalid 3D point.").arg(path);
            return result;
        }
        poses.points.push_back(*point);
    }
    if (poses.poses.empty()) {
        result.error = QStringLiteral("%1 has no camera positions.").arg(path);
        return result;
    }
    result.file = std::move(poses);
    return result;
}

}  // namespace streetpano::stitch
