#pragma once

#include <QByteArray>
#include <QList>
#include <QSet>
#include <QString>

namespace omachat::client {

// Artwork alone: authorization comes from the current account's model, never
// from possession of a file. IDs are immutable; replacement changes the ID.
class ArtworkCache {
public:
    struct Limits {
        qint64 bytes = 64 * 1024 * 1024;
        int entries = 256;
        qint64 imageBytes = 10 * 1024 * 1024;
    };
    ArtworkCache();
    explicit ArtworkCache(QString root);
    ArtworkCache(QString root, Limits limits);

    // Returns IDs whose UI URLs must be removed. A changed scope invalidates
    // every old artwork request, even if the new account uses identical IDs.
    QSet<QString> reconcile(const QString& identity, const QSet<QString>& authorized, const QSet<QString>& active,
        bool authorizationKnown = true);
    quint64 generation() const { return m_generation; }
    QString scopeFingerprint() const { return m_scope; }
    bool authorized(const QString& id) const { return !m_scope.isEmpty() && m_authorized.contains(id); }
    // Reconciliation changes authorization immediately, without scanning disk.
    // Workers plan cleanup from immutable snapshots; the owner applies at most
    // 32 candidates per turn, rejecting stale policy or substituted files.
    struct CleanupEntry {
        QString path;
        QByteArray stamp, directoryStamp;
        qint64 bytes = 0, modified = 0;
        bool expired = false, revoked = false;
    };
    struct Cleanup {
        QString root, scope, preserve;
        QByteArray rootStamp;
        QSet<QString> allowed, pins, staging;
        QList<CleanupEntry> entries;
        quint64 revision = 0;
        qint64 total = 0, byteLimit = 0, reserveBytes = 0;
        int count = 0, entryLimit = 0, reserveEntries = 0;
        qsizetype cursor = 0;
        bool known = false, valid = false;
    };
    enum class CleanupResult { Done, More, Stale };
    bool cleanupPending() const { return m_cleanupPending; }
    Cleanup prepareCleanup() const;
    static Cleanup scanCleanup(Cleanup cleanup);
    CleanupResult finishCleanup(Cleanup& cleanup);
    // Prepare/finish run on the owner thread. Validation owns only a snapshot
    // and may run on a worker; it never mutates files or cache state.
    struct Lookup {
        QString id, path, root, scope;
        QByteArray stamp;
        quint64 generation = 0;
        qint64 imageBytes = 0;
        bool valid = false;
    };
    Lookup prepareLookup(const QString& id);
    static Lookup validateLookup(Lookup lookup);
    QString finishLookup(const Lookup& lookup);
    QString lookup(const QString& id);
    bool stagingFull() const { return m_staging.size() >= 16; }
    QString stagingPath(const QString& id);
    struct Store {
        Lookup staging, destination;
        Cleanup cleanup;
    };
    Store prepareStore(const QString& id, const QString& staging, quint64 generation);
    static Store validateStore(Store store);
    Store prepareStoreCleanup(Store store) const;
    static Store scanStoreCleanup(Store store);
    // Batched owner-thread admission; Stale requests a fresh worker scan.
    CleanupResult finishStore(Store& store, QString& path);
    QString finishStore(const Store& store);
    QString store(const QString& id, const QString& staging, quint64 generation);
    void discard(const QString& staging);
    void invalidate(const QString& id);

private:
    QString entryPath(const QString& id) const;
    QString m_root;
    QString m_scope;
    Limits m_limits;
    QSet<QString> m_authorized;
    QSet<QString> m_active;
    QSet<QString> m_staging;
    bool m_authorizationKnown = false;
    quint64 m_generation = 0;
    quint64 m_revision = 0;
    bool m_cleanupPending = false;
};

} // namespace omachat::client
