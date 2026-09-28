#include "storage/LocalStore.hpp"

#include "omachat/core/Log.hpp"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace omachat::daemon {

namespace {
constexpr int kSchemaVersion = 1;
}

LocalStore::LocalStore()
    : m_connection(QUuid::createUuid().toString())
{
}

LocalStore::~LocalStore()
{
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connection);
}

bool LocalStore::exec(const QString& sql)
{
    QSqlQuery q(m_db);
    if (!q.exec(sql)) {
        OMA_ERROR("storage", "local statement failed", {"error", q.lastError().text()});
        return false;
    }
    return true;
}

bool LocalStore::open(const QString& path, QString* error)
{
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    m_db.setDatabaseName(path);
    if (!m_db.open()) {
        if (error)
            *error = m_db.lastError().text();
        return false;
    }
    exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    exec(QStringLiteral("PRAGMA foreign_keys=ON"));

    QSqlQuery v(m_db);
    int version = 0;
    if (v.exec(QStringLiteral("PRAGMA user_version")) && v.next())
        version = v.value(0).toInt();
    if (version > kSchemaVersion) {
        if (error)
            *error = QStringLiteral("local database is from a newer OmaChat");
        return false;
    }
    if (version < 1) {
        const bool ok
            = exec(QStringLiteral("CREATE TABLE accounts(id INTEGER PRIMARY KEY AUTOINCREMENT, host TEXT NOT NULL, "
                                  "port INTEGER NOT NULL, username TEXT NOT NULL, trusted_fingerprint TEXT NOT NULL "
                                  "DEFAULT '', last_used INTEGER NOT NULL DEFAULT 0, UNIQUE(host, port, username))"))
            && exec(QStringLiteral("CREATE TABLE user_volume(account_id INTEGER NOT NULL REFERENCES accounts(id) "
                                   "ON DELETE CASCADE, user_id INTEGER NOT NULL, gain REAL NOT NULL, "
                                   "PRIMARY KEY(account_id, user_id))"))
            && exec(
                QStringLiteral("CREATE TABLE muted_channels(account_id INTEGER NOT NULL REFERENCES accounts(id) "
                               "ON DELETE CASCADE, channel_id INTEGER NOT NULL, PRIMARY KEY(account_id, channel_id))"))
            && exec(QStringLiteral("CREATE TABLE kv(key TEXT PRIMARY KEY, value TEXT NOT NULL)"))
            && exec(QStringLiteral("PRAGMA user_version=%1").arg(kSchemaVersion));
        if (!ok) {
            if (error)
                *error = QStringLiteral("cannot initialize local database");
            return false;
        }
    }
    return true;
}

namespace {
Account readAccount(const QSqlQuery& q)
{
    return Account{q.value(0).toLongLong(), q.value(1).toString(), static_cast<quint16>(q.value(2).toUInt()),
        q.value(3).toString(), q.value(4).toString(), q.value(5).toLongLong()};
}
constexpr const char* kAccountCols = "id, host, port, username, trusted_fingerprint, last_used";
} // namespace

std::vector<Account> LocalStore::accounts()
{
    std::vector<Account> out;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT %1 FROM accounts ORDER BY last_used DESC, id").arg(QLatin1StringView(kAccountCols)));
    while (q.next())
        out.push_back(readAccount(q));
    return out;
}

std::optional<Account> LocalStore::account(std::int64_t id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM accounts WHERE id = ?").arg(QLatin1StringView(kAccountCols)));
    q.addBindValue(qint64(id));
    if (q.exec() && q.next())
        return readAccount(q);
    return std::nullopt;
}

std::optional<Account> LocalStore::findAccount(const QString& host, quint16 port, const QString& username)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM accounts WHERE host = ? AND port = ? AND username = ?")
            .arg(QLatin1StringView(kAccountCols)));
    q.addBindValue(host);
    q.addBindValue(port);
    q.addBindValue(username);
    if (q.exec() && q.next())
        return readAccount(q);
    return std::nullopt;
}

std::int64_t LocalStore::addAccount(const Account& a)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO accounts(host, port, username, last_used) VALUES(?,?,?,?)"));
    q.addBindValue(a.host);
    q.addBindValue(a.port);
    q.addBindValue(a.username);
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!q.exec())
        return 0;
    return q.lastInsertId().toLongLong();
}

bool LocalStore::removeAccount(std::int64_t id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM accounts WHERE id = ?"));
    q.addBindValue(qint64(id));
    return q.exec() && q.numRowsAffected() == 1;
}

bool LocalStore::setTrustedFingerprint(std::int64_t id, const QString& fingerprint)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE accounts SET trusted_fingerprint = ? WHERE id = ?"));
    q.addBindValue(fingerprint);
    q.addBindValue(qint64(id));
    return q.exec() && q.numRowsAffected() == 1;
}

bool LocalStore::touchAccount(std::int64_t id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE accounts SET last_used = ? WHERE id = ?"));
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    q.addBindValue(qint64(id));
    return q.exec();
}

std::map<std::uint64_t, double> LocalStore::userVolumes(std::int64_t accountId)
{
    std::map<std::uint64_t, double> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT user_id, gain FROM user_volume WHERE account_id = ?"));
    q.addBindValue(qint64(accountId));
    q.exec();
    while (q.next())
        out[static_cast<std::uint64_t>(q.value(0).toLongLong())] = q.value(1).toDouble();
    return out;
}

bool LocalStore::setUserVolume(std::int64_t accountId, std::uint64_t userId, double gain)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO user_volume(account_id, user_id, gain) VALUES(?,?,?) "
                             "ON CONFLICT(account_id, user_id) DO UPDATE SET gain = excluded.gain"));
    q.addBindValue(qint64(accountId));
    q.addBindValue(qint64(userId));
    q.addBindValue(gain);
    return q.exec();
}

std::set<std::uint64_t> LocalStore::mutedChannels(std::int64_t accountId)
{
    std::set<std::uint64_t> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT channel_id FROM muted_channels WHERE account_id = ?"));
    q.addBindValue(qint64(accountId));
    q.exec();
    while (q.next())
        out.insert(static_cast<std::uint64_t>(q.value(0).toLongLong()));
    return out;
}

bool LocalStore::setChannelMuted(std::int64_t accountId, std::uint64_t channelId, bool muted)
{
    QSqlQuery q(m_db);
    if (muted)
        q.prepare(QStringLiteral("INSERT OR IGNORE INTO muted_channels(account_id, channel_id) VALUES(?,?)"));
    else
        q.prepare(QStringLiteral("DELETE FROM muted_channels WHERE account_id = ? AND channel_id = ?"));
    q.addBindValue(qint64(accountId));
    q.addBindValue(qint64(channelId));
    return q.exec();
}

QString LocalStore::value(const QString& key, const QString& fallback)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT value FROM kv WHERE key = ?"));
    q.addBindValue(key);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return fallback;
}

bool LocalStore::setValue(const QString& key, const QString& value)
{
    QSqlQuery q(m_db);
    q.prepare(
        QStringLiteral("INSERT INTO kv(key, value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    q.addBindValue(key);
    q.addBindValue(value);
    return q.exec();
}

} // namespace omachat::daemon
