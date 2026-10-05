#pragma once

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
    bool authorized(const QString& id) const { return !m_scope.isEmpty() && m_authorized.contains(id); }
    QString lookup(const QString& id);
    bool stagingFull() const { return m_staging.size() >= 16; }
    QString stagingPath(const QString& id);
    QString store(const QString& id, const QString& staging, quint64 generation);
    void discard(const QString& staging);
    void invalidate(const QString& id);

private:
    QString entryPath(const QString& id) const;
    bool validImage(const QString& path) const;
    bool prune(qint64 reserveBytes = 0, int reserveEntries = 0, const QString& preserve = {});
    QString m_root;
    QString m_scope;
    Limits m_limits;
    QSet<QString> m_authorized;
    QSet<QString> m_active;
    QSet<QString> m_staging;
    bool m_authorizationKnown = false;
    quint64 m_generation = 0;
};

} // namespace omachat::client
