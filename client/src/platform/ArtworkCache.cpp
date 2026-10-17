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
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
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
// Include inode and nanosecond change time: same-size replacement and edits
// that restore mtime must not turn a stale decoder result into a valid URL.
QByteArray statStamp(const struct stat& st)
{
    return QByteArray::number(st.st_dev) + '/' + QByteArray::number(st.st_ino) + '/' + QByteArray::number(st.st_size)
        + '/' + QByteArray::number(st.st_mtim.tv_sec) + '/' + QByteArray::number(st.st_mtim.tv_nsec) + '/'
        + QByteArray::number(st.st_ctim.tv_sec) + '/' + QByteArray::number(st.st_ctim.tv_nsec);
}
QByteArray fileStamp(const QString& path)
{
    struct stat st{};
    if (::lstat(QFile::encodeName(path).constData(), &st) || !S_ISREG(st.st_mode))
        return {};
    return statStamp(st);
}
bool ownedDirectory(const QString& path)
{
    struct stat st{};
    return ::lstat(QFile::encodeName(path).constData(), &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == ::getuid();
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
    ++m_revision;
    m_cleanupPending = true;
    return removed;
}

namespace {
QByteArray directoryStamp(const QString& path)
{
    struct stat st{};
    if (::lstat(QFile::encodeName(path).constData(), &st) || !S_ISDIR(st.st_mode) || st.st_uid != ::getuid())
        return {};
    return QByteArray::number(st.st_dev) + '/' + QByteArray::number(st.st_ino);
}
QByteArray directoryStamp(const struct stat& st)
{
    return QByteArray::number(st.st_dev) + '/' + QByteArray::number(st.st_ino);
}
} // namespace

ArtworkCache::Cleanup ArtworkCache::prepareCleanup() const
{
    Cleanup cleanup;
    cleanup.root = m_root;
    cleanup.scope = m_scope.isEmpty() ? QString() : m_root + u'/' + m_scope;
    cleanup.revision = m_revision;
    cleanup.known = m_authorizationKnown;
    cleanup.staging = m_staging;
    cleanup.byteLimit = m_limits.bytes;
    cleanup.entryLimit = m_limits.entries;
    for (const auto& id : m_authorized)
        cleanup.allowed.insert(entryPath(id));
    for (const auto& id : m_active)
        cleanup.pins.insert(entryPath(id));
    return cleanup;
}

ArtworkCache::Cleanup ArtworkCache::scanCleanup(Cleanup cleanup)
{
    // Enumerate through owned no-follow descriptors. No worker changes files,
    // directory permissions, authorization or controller state.
    cleanup.valid = false;
    cleanup.entries.clear();
    cleanup.cursor = 0;
    cleanup.total = cleanup.reserveBytes;
    cleanup.count = cleanup.reserveEntries;
    const int rootFd
        = ::open(QFile::encodeName(cleanup.root).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (rootFd < 0)
        return cleanup;
    struct stat rootStat{};
    if (::fstat(rootFd, &rootStat) || rootStat.st_uid != ::getuid()) {
        ::close(rootFd);
        return cleanup;
    }
    DIR* root = ::fdopendir(rootFd);
    if (!root) {
        ::close(rootFd);
        return cleanup;
    }
    cleanup.rootStamp = directoryStamp(rootStat);
    const qint64 expiry = QDateTime::currentSecsSinceEpoch() - 24 * 60 * 60;
    bool complete = true;
    auto next = [&](DIR* directory) {
        errno = 0;
        auto* entry = ::readdir(directory);
        if (!entry && errno)
            complete = false;
        return entry;
    };
    while (const auto* directory = next(root)) {
        const QByteArray name(directory->d_name);
        if (name == "." || name == "..")
            continue;
        const int fd = ::openat(rootFd, name.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0) {
            if (errno != ENOTDIR && errno != ELOOP && errno != ENOENT)
                complete = false;
            continue;
        }
        struct stat directoryStat{};
        if (::fstat(fd, &directoryStat)) {
            complete = false;
            ::close(fd);
            continue;
        }
        if (directoryStat.st_uid != ::getuid()) {
            ::close(fd);
            continue;
        }
        DIR* files = ::fdopendir(fd);
        if (!files) {
            complete = false;
            ::close(fd);
            continue;
        }
        const QString parent = cleanup.root + u'/' + QFile::decodeName(name);
        const bool staging = name == "staging";
        while (const auto* file = next(files)) {
            const QString fileName = QFile::decodeName(file->d_name);
            struct stat st{};
            if (::fstatat(fd, file->d_name, &st, AT_SYMLINK_NOFOLLOW)) {
                if (errno != ENOENT)
                    complete = false;
                continue;
            }
            if (!S_ISREG(st.st_mode) || st.st_uid != ::getuid())
                continue;
            const QString path = parent + u'/' + fileName;
            if (path == cleanup.preserve)
                continue;
            const bool revoked
                = !staging && cleanup.known && parent == cleanup.scope && !cleanup.allowed.contains(path);
            const bool cacheFile = fileName.endsWith(u".image") || fileName.endsWith(u".image.part");
            if (!revoked && !cacheFile)
                continue;
            const QString owner = path.endsWith(u".part") ? path.chopped(5) : path;
            const bool expired = staging && !cleanup.staging.contains(owner) && st.st_mtim.tv_sec < expiry;
            if (staging && !expired)
                continue;
            cleanup.entries.append(
                {path, statStamp(st), directoryStamp(directoryStat), st.st_size, st.st_mtim.tv_sec, expired, revoked});
            if (!staging) {
                cleanup.total += st.st_size;
                ++cleanup.count;
            }
        }
        ::closedir(files);
    }
    ::closedir(root);
    std::sort(cleanup.entries.begin(), cleanup.entries.end(), [](const CleanupEntry& a, const CleanupEntry& b) {
        if ((a.expired || a.revoked) != (b.expired || b.revoked))
            return a.expired || a.revoked;
        return a.modified < b.modified;
    });
    cleanup.valid = complete && directoryStamp(cleanup.root) == cleanup.rootStamp;
    return cleanup;
}

ArtworkCache::CleanupResult ArtworkCache::finishCleanup(Cleanup& cleanup)
{
    if (cleanup.revision != m_revision)
        return CleanupResult::Stale;
    if (!cleanup.valid || directoryStamp(m_root) != cleanup.rootStamp) {
        m_cleanupPending = false;
        return CleanupResult::Done;
    }
    const qsizetype end = std::min(cleanup.cursor + 32, cleanup.entries.size());
    for (; cleanup.cursor < end; ++cleanup.cursor) {
        const auto& entry = cleanup.entries[cleanup.cursor];
        if (!entry.expired && !entry.revoked
            && (cleanup.total <= cleanup.byteLimit && cleanup.count <= cleanup.entryLimit)) {
            cleanup.cursor = cleanup.entries.size();
            break;
        }
        if (cleanup.pins.contains(entry.path) && !entry.revoked)
            continue;
        const QString parent = QFileInfo(entry.path).absolutePath();
        if (directoryStamp(parent) != entry.directoryStamp || fileStamp(entry.path) != entry.stamp)
            continue;
        // Reopen by descriptor for deletion, too: path substitution must never
        // redirect unlink into another tree after the identity check.
        const int root = ::open(QFile::encodeName(m_root).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (root < 0)
            continue;
        struct stat st{};
        if (::fstat(root, &st) || directoryStamp(st) != cleanup.rootStamp) {
            ::close(root);
            continue;
        }
        const int directory = ::openat(root, QFile::encodeName(QFileInfo(parent).fileName()).constData(),
            O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        ::close(root);
        if (directory < 0)
            continue;
        const QByteArray name = QFile::encodeName(QFileInfo(entry.path).fileName());
        const bool same = ::fstat(directory, &st) == 0 && st.st_uid == ::getuid()
            && directoryStamp(st) == entry.directoryStamp
            && ::fstatat(directory, name.constData(), &st, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(st.st_mode)
            && statStamp(st) == entry.stamp;
        if (same && ::unlinkat(directory, name.constData(), 0) == 0 && !entry.expired) {
            cleanup.total -= entry.bytes;
            --cleanup.count;
        }
        ::close(directory);
    }
    if (cleanup.cursor < cleanup.entries.size())
        return CleanupResult::More;
    m_cleanupPending = false;
    return CleanupResult::Done;
}

ArtworkCache::Lookup ArtworkCache::prepareLookup(const QString& id)
{
    Lookup result;
    if (!authorized(id) || !privateDir(m_root) || !privateDir(m_root + u'/' + m_scope))
        return result;
    result.id = id;
    result.path = entryPath(id);
    result.root = m_root;
    result.scope = m_root + u'/' + m_scope;
    result.stamp = fileStamp(result.path);
    result.generation = m_generation;
    result.imageBytes = m_limits.imageBytes;
    return result;
}

ArtworkCache::Lookup ArtworkCache::validateLookup(Lookup lookup)
{
    lookup.valid = false;
    if (lookup.stamp.isEmpty() || !ownedDirectory(lookup.root) || !ownedDirectory(lookup.scope)
        || fileStamp(lookup.path) != lookup.stamp)
        return lookup;
    // Open relative to owned, no-follow directory descriptors so substitution
    // cannot redirect a decoder to another directory while the worker runs.
    const int root
        = ::open(QFile::encodeName(lookup.root).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (root < 0)
        return lookup;
    const int scope = ::openat(root, QFile::encodeName(QFileInfo(lookup.scope).fileName()).constData(),
        O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    ::close(root);
    if (scope < 0)
        return lookup;
    const int fd = ::openat(scope, QFile::encodeName(QFileInfo(lookup.path).fileName()).constData(),
        O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    ::close(scope);
    if (fd < 0)
        return lookup;
    QFile file;
    if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(fd);
        return lookup;
    }
    struct stat st{};
    if (::fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > lookup.imageBytes
        || statStamp(st) != lookup.stamp)
        return lookup;
    QImageReader reader(&file);
    const QSize size = reader.size();
    if (size.isValid() && qint64(size.width()) * size.height() <= 16 * 1024 * 1024)
        lookup.valid = !reader.read().isNull();
    lookup.valid = lookup.valid && ::fstat(fd, &st) == 0 && statStamp(st) == lookup.stamp && ownedDirectory(lookup.root)
        && ownedDirectory(lookup.scope) && fileStamp(lookup.path) == lookup.stamp;
    return lookup;
}

QString ArtworkCache::finishLookup(const Lookup& lookup)
{
    if (lookup.generation != m_generation || !authorized(lookup.id) || lookup.path != entryPath(lookup.id)
        || !ownedDirectory(m_root) || !ownedDirectory(m_root + u'/' + m_scope) || lookup.stamp.isEmpty()
        || fileStamp(lookup.path) != lookup.stamp)
        return {};
    if (!lookup.valid) {
        ++m_revision;
        QFile::remove(lookup.path);
        return {};
    }
    ++m_revision;
    QFile file(lookup.path);
    if (file.open(QIODevice::ReadOnly))
        file.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
    return lookup.path;
}

QString ArtworkCache::lookup(const QString& id)
{
    return finishLookup(validateLookup(prepareLookup(id)));
}

QString ArtworkCache::stagingPath(const QString& id)
{
    if (stagingFull() || !authorized(id) || !privateDir(m_root) || !privateDir(m_root + QStringLiteral("/staging")))
        return {};
    const QString path
        = m_root + QStringLiteral("/staging/") + QUuid::createUuid().toString(QUuid::Id128) + QStringLiteral(".image");
    ++m_revision;
    m_staging.insert(path);
    return path;
}

void ArtworkCache::discard(const QString& staging)
{
    ++m_revision;
    if (m_staging.remove(staging) && privateDir(m_root) && privateDir(m_root + QStringLiteral("/staging"))) {
        QFile::remove(staging);
        QFile::remove(staging + QStringLiteral(".part"));
    }
}

ArtworkCache::Store ArtworkCache::prepareStore(const QString& id, const QString& staging, quint64 generation)
{
    Store result;
    if (!m_staging.contains(staging) || generation != m_generation || !authorized(id) || !privateDir(m_root)
        || !privateDir(m_root + QStringLiteral("/staging")))
        return result;
    result.destination = prepareLookup(id);
    result.staging = result.destination;
    result.staging.path = staging;
    result.staging.scope = m_root + QStringLiteral("/staging");
    result.staging.stamp = fileStamp(staging);
    return prepareStoreCleanup(std::move(result));
}

ArtworkCache::Store ArtworkCache::validateStore(Store store)
{
    store.staging = validateLookup(store.staging);
    store.destination = validateLookup(store.destination);
    return scanStoreCleanup(std::move(store));
}

ArtworkCache::Store ArtworkCache::prepareStoreCleanup(Store store) const
{
    store.cleanup = prepareCleanup();
    store.cleanup.preserve = store.destination.path;
    store.cleanup.reserveBytes = QFileInfo(store.staging.path).size();
    store.cleanup.reserveEntries = 1;
    return store;
}

ArtworkCache::Store ArtworkCache::scanStoreCleanup(Store store)
{
    if (store.staging.valid && !store.destination.valid)
        store.cleanup = scanCleanup(std::move(store.cleanup));
    return store;
}

ArtworkCache::CleanupResult ArtworkCache::finishStore(Store& store, QString& path)
{
    const auto& staging = store.staging;
    const auto& destination = store.destination;
    path.clear();
    if (!m_staging.contains(staging.path))
        return CleanupResult::Done;
    auto reject = [&] {
        discard(staging.path);
        return CleanupResult::Done;
    };
    // Workers only decode snapshots. Recheck ownership, authorization and both
    // file identities before any GUI-thread publication or mutation.
    if (staging.generation != m_generation || !authorized(staging.id) || destination.path != entryPath(staging.id)
        || !staging.valid || !privateDir(m_root) || !privateDir(staging.scope) || !privateDir(destination.scope)
        || staging.stamp.isEmpty() || fileStamp(staging.path) != staging.stamp
        || fileStamp(destination.path) != destination.stamp)
        return reject();
    if (destination.valid) {
        reject();
        path = destination.path;
        return CleanupResult::Done;
    }
    if (store.cleanup.revision != m_revision)
        return CleanupResult::Stale;
    if (!store.cleanup.valid || directoryStamp(m_root) != store.cleanup.rootStamp)
        return reject();
    const auto result = finishCleanup(store.cleanup);
    if (result != CleanupResult::Done)
        return result;
    if (store.cleanup.total > m_limits.bytes || store.cleanup.count > m_limits.entries)
        return reject();
    ++m_revision;
    QFile::remove(destination.path);
    if (!QFile::setPermissions(staging.path, QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || !QFile::rename(staging.path, destination.path))
        return reject();
    m_staging.remove(staging.path);
    path = destination.path;
    return CleanupResult::Done;
}

QString ArtworkCache::finishStore(const Store& snapshot)
{
    // Synchronous convenience for cache unit fixtures. The GUI uses the batched
    // overload and runs every scan on the shared bounded worker pool.
    Store store = snapshot;
    QString path;
    for (;;) {
        const auto result = finishStore(store, path);
        if (result == CleanupResult::Done)
            return path;
        if (result == CleanupResult::Stale)
            store = scanStoreCleanup(prepareStoreCleanup(std::move(store)));
    }
}

QString ArtworkCache::store(const QString& id, const QString& staging, quint64 generation)
{
    const auto snapshot = prepareStore(id, staging, generation);
    const QString path = finishStore(validateStore(snapshot));
    if (path.isEmpty())
        discard(staging);
    return path;
}

void ArtworkCache::invalidate(const QString& id)
{
    ++m_revision;
    if (!m_scope.isEmpty() && privateDir(m_root) && privateDir(m_root + u'/' + m_scope))
        QFile::remove(entryPath(id));
}

} // namespace omachat::client
