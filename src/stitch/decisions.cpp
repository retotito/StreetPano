#include "stitch/decisions.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cmath>

namespace streetpano::stitch {
namespace {

constexpr int kFormatVersion = 1;
constexpr qint64 kMaxFileBytes = qint64{64} * 1024 * 1024;
constexpr int kJpegQuality = 95;
constexpr qsizetype kMinOutlinePoints = 3;

[[nodiscard]] std::optional<core::Patch> readPatch(const QJsonObject& object) {
    const QString photo = object.value(QStringLiteral("photo")).toString();
    const QString note = object.value(QStringLiteral("note")).toString();
    const QJsonArray outline = object.value(QStringLiteral("outline")).toArray();
    // Only a file name is accepted, so that a damaged file can't point outside the folder.
    if ((photo.isEmpty() && note.isEmpty()) || photo.contains(u'/') || photo.contains(u'\\') ||
        outline.size() < kMinOutlinePoints) {
        return std::nullopt;
    }
    core::Patch patch;
    patch.photo = photo.toStdString();
    patch.note = note.toStdString();
    patch.kind = object.value(QStringLiteral("kind")).toString().toStdString();
    patch.hidden = object.value(QStringLiteral("hidden")).toBool(false);
    patch.outline.reserve(static_cast<std::size_t>(outline.size()));
    for (qsizetype index = 0; index < outline.size(); ++index) {
        const QJsonArray point = outline.at(index).toArray();
        if (point.size() != 2 || !point.at(0).isDouble() || !point.at(1).isDouble()) {
            return std::nullopt;
        }
        const double arc = point.at(0).toDouble();
        const double height = point.at(1).toDouble();
        if (!std::isfinite(arc) || !std::isfinite(height)) {
            return std::nullopt;
        }
        patch.outline.push_back(core::WallPoint{arc, height});
    }
    return patch;
}

}  // namespace

QString decisionsPathFor(const QString& posesPath) {
    return QFileInfo(posesPath).absoluteDir().filePath(QStringLiteral("decisions.json"));
}

DecisionsResult readDecisions(const QString& path) {
    DecisionsResult result;
    QFile file(path);
    if (!file.exists()) {
        result.decisions = Decisions{};
        return result;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("Can't open %1: %2").arg(path, file.errorString());
        return result;
    }
    if (file.size() > kMaxFileBytes) {
        result.error = QStringLiteral("%1 is too large to be a list of edits.").arg(path);
        return result;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("%1 isn't a valid list of edits.").arg(path);
        return result;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format_version")).toInt() != kFormatVersion) {
        result.error = QStringLiteral("%1 uses a different format version.").arg(path);
        return result;
    }
    Decisions decisions;
    const QJsonArray patches = root.value(QStringLiteral("patches")).toArray();
    decisions.patches.reserve(static_cast<std::size_t>(patches.size()));
    for (qsizetype index = 0; index < patches.size(); ++index) {
        std::optional<core::Patch> patch = readPatch(patches.at(index).toObject());
        if (!patch.has_value()) {
            result.error = QStringLiteral("%1 contains an invalid edit.").arg(path);
            return result;
        }
        decisions.patches.push_back(std::move(*patch));
    }
    result.decisions = std::move(decisions);
    return result;
}

bool writeDecisions(const Decisions& decisions, const QString& path) {
    QJsonArray patches;
    for (const core::Patch& patch : decisions.patches) {
        QJsonArray outline;
        for (const core::WallPoint& point : patch.outline) {
            outline.append(QJsonArray{point.arc, point.height});
        }
        QJsonObject object{{QStringLiteral("photo"), QString::fromStdString(patch.photo)},
                           {QStringLiteral("outline"), outline}};
        if (!patch.note.empty()) {
            object.insert(QStringLiteral("note"), QString::fromStdString(patch.note));
        }
        if (!patch.kind.empty()) {
            object.insert(QStringLiteral("kind"), QString::fromStdString(patch.kind));
        }
        if (patch.hidden) {
            object.insert(QStringLiteral("hidden"), true);
        }
        patches.append(object);
    }
    const QJsonObject root{{QStringLiteral("format_version"), kFormatVersion},
                           {QStringLiteral("patches"), patches}};
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson());
    return file.commit();
}

bool savePanorama(const QImage& panorama, const QString& path) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    const QByteArray format = QFileInfo(path).suffix().toLatin1();
    if (!panorama.save(&file, format.constData(), kJpegQuality)) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

}  // namespace streetpano::stitch
