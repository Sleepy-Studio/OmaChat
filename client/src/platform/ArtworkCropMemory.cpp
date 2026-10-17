#include "ArtworkCropMemory.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace omachat::client::artwork {
namespace {
constexpr qsizetype maxBytes = 64 * 1024;
constexpr size_t maxChoices = 128;
constexpr qint64 retentionMs = 90LL * 24 * 60 * 60 * 1000;
constexpr auto ownerPermissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
bool validKey(const QString& key)
{
    return key.size() == 64
        && std::ranges::all_of(key, [](QChar c) { return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f'); });
}
bool validChoice(CropChoice c)
{
    return std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.zoom) && c.x >= 0 && c.x <= 1 && c.y >= 0
        && c.y <= 1 && c.zoom >= 1 && c.zoom <= 4;
}
bool ownedPath(const QString& path, bool directory)
{
    struct stat st{};
    return ::lstat(QFile::encodeName(path).constData(), &st) == 0 && st.st_uid == ::getuid()
        && (directory ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode)) && (st.st_mode & 0077) == 0;
}
bool prepareDirectory(const QString& directory)
{
    if (!QDir::isAbsolutePath(directory))
        return false;
    struct stat st{};
    if (::lstat(QFile::encodeName(directory).constData(), &st) == 0)
        return ownedPath(directory, true);
    // Create the application parent, then our private directory without following
    // an existing leaf symlink. Existing directories are never chmod'ed here.
    if (!QDir().mkpath(QFileInfo(directory).absolutePath()))
        return false;
    if (::mkdir(QFile::encodeName(directory).constData(), 0700) != 0)
        return ownedPath(directory, true);
    return true;
}
std::optional<QJsonObject> readChoices(const QString& directory)
{
    const QString path = directory + QStringLiteral("/choices.json");
    struct stat st{};
    if (::lstat(QFile::encodeName(path).constData(), &st) != 0)
        return errno == ENOENT ? std::optional<QJsonObject>(QJsonObject()) : std::nullopt;
    if (!ownedPath(path, false) || st.st_size > maxBytes)
        return std::nullopt;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QByteArray bytes = file.read(maxBytes + 1);
    if (bytes.size() > maxBytes)
        return std::nullopt;
    const auto doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject() || doc.object().value(QStringLiteral("version")).toInt() != 1
        || !doc.object().value(QStringLiteral("choices")).isObject())
        return std::nullopt;
    return doc.object().value(QStringLiteral("choices")).toObject();
}
std::optional<CropChoice> decode(const QJsonObject& row, qint64 now)
{
    for (const auto* field : {"x", "y", "zoom", "saved_at"})
        if (!row.value(QLatin1StringView(field)).isDouble())
            return std::nullopt;
    const double saved = row.value(QStringLiteral("saved_at")).toDouble();
    if (!std::isfinite(saved) || saved <= 0 || saved > double(now) || double(now) - saved > double(retentionMs))
        return std::nullopt;
    const CropChoice choice{row.value(QStringLiteral("x")).toDouble(), row.value(QStringLiteral("y")).toDouble(),
        row.value(QStringLiteral("zoom")).toDouble()};
    return validChoice(choice) ? std::optional<CropChoice>(choice) : std::nullopt;
}
} // namespace

QString cropMemoryKey(const QString& identity, const QString& server, const QString& pixels, const QString& kind)
{
    if (identity.isEmpty() || server.isEmpty() || !validKey(pixels) || (kind != u"icon" && kind != u"banner"))
        return {};
    const QByteArray input = QJsonDocument(QJsonArray{identity, server, pixels, kind}).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(input, QCryptographicHash::Sha256).toHex());
}

std::optional<CropChoice> loadCropChoice(const QString& directory, const QString& key)
{
    if (!validKey(key) || !ownedPath(directory, true))
        return std::nullopt;
    QLockFile lock(directory + QStringLiteral("/choices.lock"));
    if (!lock.tryLock(250))
        return std::nullopt;
    const auto choices = readChoices(directory);
    return choices ? decode(choices->value(key).toObject(), QDateTime::currentMSecsSinceEpoch()) : std::nullopt;
}

bool saveCropChoice(const QString& directory, const QString& key, CropChoice choice)
{
    if (!validKey(key) || !validChoice(choice) || !prepareDirectory(directory))
        return false;
    QLockFile lock(directory + QStringLiteral("/choices.lock"));
    if (!lock.tryLock(250))
        return false;
    auto choices = readChoices(directory);
    if (!choices)
        return false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    std::vector<std::pair<QString, QJsonObject>> kept;
    for (auto it = choices->begin(); it != choices->end(); ++it)
        if (it.key() != key && validKey(it.key()) && decode(it.value().toObject(), now))
            kept.emplace_back(it.key(), it.value().toObject());
    std::ranges::sort(kept, [](const auto& a, const auto& b) {
        const double left = a.second.value(QStringLiteral("saved_at")).toDouble();
        const double right = b.second.value(QStringLiteral("saved_at")).toDouble();
        return left != right ? left > right : a.first < b.first;
    });
    QJsonObject result;
    for (size_t i = 0; i < std::min(kept.size(), maxChoices - 1); ++i)
        result.insert(kept[i].first, kept[i].second);
    result.insert(key, QJsonObject{{"x", choice.x}, {"y", choice.y}, {"zoom", choice.zoom}, {"saved_at", double(now)}});
    const QByteArray bytes
        = QJsonDocument(QJsonObject{{"version", 1}, {"choices", result}}).toJson(QJsonDocument::Compact);
    if (bytes.size() > maxBytes)
        return false;
    QSaveFile file(directory + QStringLiteral("/choices.json"));
    file.setDirectWriteFallback(false);
    return file.open(QIODevice::WriteOnly) && file.setPermissions(ownerPermissions) && file.write(bytes) == bytes.size()
        && file.commit();
}
} // namespace omachat::client::artwork
