#include "platform/ArtworkCache.hpp"

#include "omachat/core/Paths.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QUuid>

#include <algorithm>
#include <unistd.h>

namespace omachat::client {
namespace {
QString digest(const QString& value)
{
    return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex());
}
bool privateDir(const QString& path)
{
    // Do not follow a substituted cache directory or chmod another user's files.
    const QFileInfo info(path);
    return !info.isSymLink() && paths::ensurePrivateDir(path) && QFileInfo(path).ownerId() == ::getuid();
}
} // namespace

ArtworkCache::ArtworkCache()
    : ArtworkCache(paths::cacheDir() + QStringLiteral("/artwork-v1"))
{
}
ArtworkCache::ArtworkCache(QString root)
    : ArtworkCache(std::move(root), Limits{})
{
}
ArtworkCache::ArtworkCache(QString root, Limits limits)
    : m_root(std::move(root))
    , m_limits(limits)
{
}

QString ArtworkCache::entryPath(const QString& id) const
{
    return m_root + u'/' + m_scope + u'/' + digest(id) + QStringLiteral(".image");
}

QSet<QString> ArtworkCache::reconcile(
    const QString& identity, const QSet<QString>& authorizedIds, const QSet<QString>& active, bool authorizationKnown)
{
    const QString scope = identity.isEmpty() ? QString() : digest(identity);
    QSet<QString> effectiveActive = active;
    effectiveActive.intersect(authorizedIds);
    if (scope == m_scope && authorizedIds == m_authorized && effectiveActive == m_active
        && authorizationKnown == m_authorizationKnown)
        return {};
    QSet<QString> removed = m_authorized;
    if (scope != m_scope || (authorizationKnown && authorizedIds.isEmpty() && !m_authorized.isEmpty())) {
        ++m_generation;
        m_scope = scope;
        m_authorized.clear();
        m_active.clear();
    } else {
        removed.subtract(authorizedIds);
    }
    m_authorizationKnown = authorizationKnown;
    m_authorized = scope.isEmpty() ? QSet<QString>() : authorizedIds;
    m_active = active;
    m_active.intersect(m_authorized);
    if (authorizationKnown && !m_scope.isEmpty() && privateDir(m_root) && privateDir(m_root + u'/' + m_scope)) {
        QSet<QString> allowedPaths;
        for (const auto& id : m_authorized)
            allowedPaths.insert(entryPath(id));
        // Also remove revoked files loaded by a previous GUI process.
        for (const auto& file : QDir(m_root + u'/' + m_scope).entryInfoList(QDir::Files | QDir::NoSymLinks))
            if (!allowedPaths.contains(file.absoluteFilePath()))
                QFile::remove(file.absoluteFilePath());
    }
    prune();
    return removed;
}

bool ArtworkCache::validImage(const QString& path) const
{
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink() || info.size() <= 0 || info.size() > m_limits.imageBytes)
        return false;
    QImageReader reader(path);
    const QSize size = reader.size();
    // Bound decoder memory independently of the compressed file size.
    if (!size.isValid() || qint64(size.width()) * size.height() > 16 * 1024 * 1024)
        return false;
    return !reader.read().isNull();
}

QString ArtworkCache::lookup(const QString& id)
{
    if (!authorized(id) || !privateDir(m_root) || !privateDir(m_root + u'/' + m_scope))
        return {};
    const QString path = entryPath(id);
    if (!validImage(path)) {
        QFile::remove(path);
        return {};
    }
    QFile file(path);
    if (file.open(QIODevice::ReadOnly))
        file.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
    return path;
}

QString ArtworkCache::stagingPath(const QString& id)
{
    if (stagingFull() || !authorized(id) || !privateDir(m_root) || !privateDir(m_root + QStringLiteral("/staging")))
        return {};
    const QString path
        = m_root + QStringLiteral("/staging/") + QUuid::createUuid().toString(QUuid::Id128) + QStringLiteral(".image");
    m_staging.insert(path);
    return path;
}

void ArtworkCache::discard(const QString& staging)
{
    if (m_staging.remove(staging) && privateDir(m_root) && privateDir(m_root + QStringLiteral("/staging"))) {
        QFile::remove(staging);
        QFile::remove(staging + QStringLiteral(".part"));
    }
}

QString ArtworkCache::store(const QString& id, const QString& staging, quint64 generation)
{
    if (!m_staging.remove(staging))
        return {};
    auto reject = [&] {
        if (privateDir(m_root) && privateDir(m_root + QStringLiteral("/staging"))) {
            QFile::remove(staging);
            QFile::remove(staging + QStringLiteral(".part"));
        }
        return QString();
    };
    // Registered paths are only ours while both parent directories remain ours.
    // Check before decoding, chmod, rename, or rejection cleanup.
    if (!privateDir(m_root) || !privateDir(m_root + QStringLiteral("/staging")))
        return {};
    if (generation != m_generation || !authorized(id) || !validImage(staging))
        return reject();
    if (!privateDir(m_root) || !privateDir(m_root + u'/' + m_scope))
        return reject();
    const QString dest = entryPath(id);
    // Same immutable ID is already visible: preserve its path and bytes.
    if (validImage(dest)) {
        reject();
        return dest;
    }
    QFile::remove(dest);
    if (!privateDir(m_root + u'/' + m_scope) || !prune(QFileInfo(staging).size(), 1, dest))
        return reject();
    if (!QFile::setPermissions(staging, QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || !QFile::rename(staging, dest))
        return reject();
    return dest;
}

void ArtworkCache::invalidate(const QString& id)
{
    if (!m_scope.isEmpty() && privateDir(m_root) && privateDir(m_root + u'/' + m_scope))
        QFile::remove(entryPath(id));
}

bool ArtworkCache::prune(qint64 reserveBytes, int reserveEntries, const QString& preserve)
{
    if (!privateDir(m_root))
        return false;
    QList<QFileInfo> entries;
    qint64 total = reserveBytes;
    int count = reserveEntries;
    QSet<QString> pins;
    for (const auto& id : m_active)
        pins.insert(entryPath(id));
    // Cache files live directly inside an owned scope or the staging directory.
    // Never recurse through a substituted directory or unrelated nested tree.
    for (const auto& directory : QDir(m_root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        if (!privateDir(directory.absoluteFilePath()))
            continue;
        for (const auto& file : QDir(directory.absoluteFilePath())
                 .entryInfoList(QStringList{QStringLiteral("*.image"), QStringLiteral("*.image.part")},
                     QDir::Files | QDir::NoSymLinks)) {
            if (file.absolutePath() == m_root + QStringLiteral("/staging")) {
                // This process owns active staging; abandoned staging expires after
                // a day, safely beyond a transfer's lifetime after GUI restart.
                const QString ownedStage = file.absoluteFilePath().endsWith(u".part")
                    ? file.absoluteFilePath().chopped(5)
                    : file.absoluteFilePath();
                if (!m_staging.contains(ownedStage)
                    && file.lastModified() < QDateTime::currentDateTimeUtc().addDays(-1))
                    QFile::remove(file.absoluteFilePath());
                continue;
            }
            entries.append(file);
            total += file.size();
            ++count;
        }
    }
    std::sort(entries.begin(), entries.end(),
        [](const QFileInfo& a, const QFileInfo& b) { return a.lastModified() < b.lastModified(); });
    for (const auto& file : entries) {
        if (total <= m_limits.bytes && count <= m_limits.entries)
            break;
        if (pins.contains(file.absoluteFilePath()) || file.absoluteFilePath() == preserve)
            continue;
        if (QFile::remove(file.absoluteFilePath())) {
            total -= file.size();
            --count;
        }
    }
    return total <= m_limits.bytes && count <= m_limits.entries;
}
} // namespace omachat::client
