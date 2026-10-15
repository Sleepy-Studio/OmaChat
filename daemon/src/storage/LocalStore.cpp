#include "storage/LocalStore.hpp"

#include "omachat/core/Log.hpp"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace omachat::daemon {

namespace {
constexpr int kSchemaVersion = 4;
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
    if (!exec(QStringLiteral("PRAGMA journal_mode=WAL")) || !exec(QStringLiteral("PRAGMA foreign_keys=ON"))) {
        if (error)
            *error = QStringLiteral("cannot configure local database");
        return false;
    }

    QSqlQuery v(m_db);
    int version = 0;
    if (!v.exec(QStringLiteral("PRAGMA user_version")) || !v.next()) {
        if (error)
            *error = QStringLiteral("cannot read local database version");
        return false;
    }
    version = v.value(0).toInt();
    v.finish();
    if (version > kSchemaVersion) {
        if (error)
            *error = QStringLiteral("local database is from a newer OmaChat");
        return false;
    }
    if (version == kSchemaVersion)
        return true;
    if (!m_db.transaction()) {
        if (error)
            *error = QStringLiteral("cannot begin local database migration");
        return false;
    }
    const auto failed = [this, error] {
        m_db.rollback();
        if (error)
            *error = QStringLiteral("cannot migrate local database");
        return false;
    };
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
            && exec(QStringLiteral("CREATE TABLE kv(key TEXT PRIMARY KEY, value TEXT NOT NULL)"));
        if (!ok)
            return failed();
    }
    if (version < 2) {
        // Device keys seen per contact (trust on first use) and whether the
        // user compared safety numbers for them.
        const bool ok = exec(QStringLiteral(
            "CREATE TABLE known_keys(account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE, "
            "user_id INTEGER NOT NULL, public_key BLOB NOT NULL, verified INTEGER NOT NULL DEFAULT 0, "
            "PRIMARY KEY(account_id, user_id, public_key))"));
        if (!ok)
            return failed();
    }
    if (version < 3) {
        const bool ok
            = exec(QStringLiteral("CREATE TABLE channel_preferences(account_id INTEGER NOT NULL REFERENCES "
                                  "accounts(id) ON DELETE CASCADE, "
                                  "channel_id INTEGER NOT NULL, notification_level TEXT NOT NULL DEFAULT 'default' "
                                  "CHECK(notification_level IN ('default','all','mentions','none')), "
                                  "mute_until_ms INTEGER CHECK(mute_until_ms IS NULL OR mute_until_ms >= 0), "
                                  "PRIMARY KEY(account_id, channel_id))"))
            && exec(QStringLiteral("INSERT INTO channel_preferences(account_id,channel_id,mute_until_ms) "
                                   "SELECT account_id,channel_id,0 FROM muted_channels"))
            && exec(QStringLiteral("DROP TABLE muted_channels"));
        if (!ok)
            return failed();
    }
    if (version < 4) {
        if (!exec(QStringLiteral(
                "CREATE TABLE sidebar_channels(account_id INTEGER NOT NULL REFERENCES accounts(id) "
                "ON DELETE CASCADE, channel_id INTEGER NOT NULL, "
                "favorite_rank INTEGER CHECK(favorite_rank IS NULL OR favorite_rank >= 0), "
                "hidden INTEGER NOT NULL CHECK(hidden IN (0,1)), "
                "collapsed INTEGER NOT NULL CHECK(collapsed IN (0,1)), PRIMARY KEY(account_id,channel_id))"))
            || !exec(QStringLiteral(
                "CREATE TABLE sidebar_views(account_id INTEGER PRIMARY KEY REFERENCES accounts(id) "
                "ON DELETE CASCADE, show_voice_participants INTEGER NOT NULL "
                "CHECK(show_voice_participants IN (0,1)), compact INTEGER NOT NULL CHECK(compact IN (0,1)))")))
            return failed();
    }
    if (!exec(QStringLiteral("PRAGMA user_version=%1").arg(kSchemaVersion)) || !m_db.commit())
        return failed();
    return true;
}

LocalStore::KnownKeys LocalStore::knownKeys(std::int64_t accountId, std::uint64_t userId)
{
    KnownKeys out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT public_key, verified FROM known_keys WHERE account_id = ? AND user_id = ?"));
    q.addBindValue(static_cast<qint64>(accountId));
    q.addBindValue(static_cast<qint64>(userId));
    q.exec();
    out.verified = true;
    while (q.next()) {
        out.keys.push_back(q.value(0).toByteArray());
        out.verified = out.verified && q.value(1).toBool();
    }
    out.verified = out.verified && !out.keys.empty();
    return out;
}

bool LocalStore::setKnownKeys(
    std::int64_t accountId, std::uint64_t userId, const std::vector<QByteArray>& keys, bool verified)
{
    m_db.transaction();
    QSqlQuery del(m_db);
    del.prepare(QStringLiteral("DELETE FROM known_keys WHERE account_id = ? AND user_id = ?"));
    del.addBindValue(static_cast<qint64>(accountId));
    del.addBindValue(static_cast<qint64>(userId));
    bool ok = del.exec();
    for (const auto& k : keys) {
        QSqlQuery ins(m_db);
        ins.prepare(
            QStringLiteral("INSERT INTO known_keys(account_id, user_id, public_key, verified) VALUES(?,?,?,?)"));
        ins.addBindValue(static_cast<qint64>(accountId));
        ins.addBindValue(static_cast<qint64>(userId));
        ins.addBindValue(k);
        ins.addBindValue(verified ? 1 : 0);
        ok = ok && ins.exec();
    }
    return ok ? m_db.commit() : (m_db.rollback(), false);
}

namespace {
Account readAccount(const QSqlQuery& q)
{
    return Account{q.value(0).toLongLong(), q.value(1).toString(), static_cast<quint16>(q.value(2).toUInt()),
        q.value(3).toString(), q.value(4).toString(), q.value(5).toLongLong()};
}
constexpr const char* kAccountCols = "id, host, port, username, trusted_fingerprint, last_used";
} // namespace

std::vector<Account> LocalStore::accounts() const
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
    q.prepare(QStringLiteral("SELECT %1 FROM accounts WHERE host = ? COLLATE NOCASE AND port = ? AND username = ?")
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

bool LocalStore::setUsername(std::int64_t id, const QString& username)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE accounts SET username = ? WHERE id = ?"));
    q.addBindValue(username);
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
    return mutedChannels(accountId, QDateTime::currentMSecsSinceEpoch());
}

std::set<std::uint64_t> LocalStore::mutedChannels(std::int64_t accountId, std::int64_t nowMs)
{
    std::set<std::uint64_t> out;
    if (const auto preferences = channelPreferences(accountId)) {
        for (const auto& [id, preference] : *preferences) {
            if (preference.muted(nowMs))
                out.insert(id);
        }
    }
    return out;
}

std::optional<std::map<std::uint64_t, ChannelPreferences>> LocalStore::channelPreferences(std::int64_t accountId)
{
    std::map<std::uint64_t, ChannelPreferences> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT channel_id,notification_level,mute_until_ms FROM channel_preferences WHERE account_id=?"));
    q.addBindValue(qint64(accountId));
    if (!q.exec())
        return std::nullopt;
    while (q.next()) {
        const auto level = parseNotificationLevel(q.value(1).toString().toStdString());
        if (!level)
            return std::nullopt;
        ChannelPreferences preference;
        preference.notificationLevel = *level;
        if (!q.value(2).isNull())
            preference.muteUntilMs = q.value(2).toLongLong();
        out.emplace(static_cast<std::uint64_t>(q.value(0).toLongLong()), preference);
    }
    if (q.lastError().isValid())
        return std::nullopt;
    return out;
}

std::optional<ChannelPreferences> LocalStore::channelPreferences(std::int64_t accountId, std::uint64_t channelId)
{
    const auto preferences = channelPreferences(accountId);
    if (!preferences)
        return std::nullopt;
    const auto it = preferences->find(channelId);
    return it == preferences->end() ? ChannelPreferences{} : it->second;
}

bool LocalStore::setChannelPreferences(
    std::int64_t accountId, std::uint64_t channelId, const ChannelPreferences& preferences)
{
    const auto name = notificationLevelName(preferences.notificationLevel);
    if (name.empty() || (preferences.muteUntilMs && *preferences.muteUntilMs < 0))
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO channel_preferences(account_id,channel_id,notification_level,mute_until_ms) "
                             "VALUES(?,?,?,?) ON CONFLICT(account_id,channel_id) DO UPDATE SET "
                             "notification_level=excluded.notification_level,mute_until_ms=excluded.mute_until_ms"));
    q.addBindValue(qint64(accountId));
    q.addBindValue(qint64(channelId));
    q.addBindValue(QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size())));
    q.addBindValue(
        preferences.muteUntilMs ? QVariant(qint64(*preferences.muteUntilMs)) : QVariant(QMetaType::fromType<qint64>()));
    return q.exec();
}

bool LocalStore::setChannelMuted(std::int64_t accountId, std::uint64_t channelId, bool muted)
{
    auto preferences = channelPreferences(accountId, channelId);
    if (!preferences)
        return false;
    preferences->muteUntilMs = muted ? std::optional<std::int64_t>(0) : std::nullopt;
    return setChannelPreferences(accountId, channelId, *preferences);
}

std::optional<SidebarPreferences> LocalStore::sidebarPreferences(std::int64_t accountId)
{
    SidebarPreferences out;
    QSqlQuery q(m_db);
    q.prepare(
        QStringLiteral("SELECT channel_id,favorite_rank,hidden,collapsed FROM sidebar_channels WHERE account_id=?"));
    q.addBindValue(qint64(accountId));
    if (!q.exec())
        return std::nullopt;
    while (q.next()) {
        SidebarChannelPreferences channel;
        if (!q.value(1).isNull())
            channel.favoriteRank = q.value(1).toLongLong();
        channel.hidden = q.value(2).toBool();
        channel.collapsed = q.value(3).toBool();
        out.channels.emplace(static_cast<std::uint64_t>(q.value(0).toLongLong()), channel);
    }
    if (q.lastError().isValid())
        return std::nullopt;
    q.prepare(QStringLiteral("SELECT show_voice_participants,compact FROM sidebar_views WHERE account_id=?"));
    q.addBindValue(qint64(accountId));
    if (!q.exec())
        return std::nullopt;
    if (q.next()) {
        out.showVoiceParticipants = q.value(0).toBool();
        out.compact = q.value(1).toBool();
    }
    if (q.lastError().isValid())
        return std::nullopt;
    return out;
}

bool LocalStore::setSidebarPreferences(std::int64_t accountId, const SidebarPreferences& preferences)
{
    if (!m_db.transaction())
        return false;
    const auto failed = [this] {
        m_db.rollback();
        return false;
    };
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM sidebar_channels WHERE account_id=?"));
    q.addBindValue(qint64(accountId));
    if (!q.exec())
        return failed();
    for (const auto& [id, channel] : preferences.channels) {
        if (channel.favoriteRank && *channel.favoriteRank < 0)
            return failed();
        q.prepare(QStringLiteral("INSERT INTO sidebar_channels VALUES(?,?,?,?,?)"));
        q.addBindValue(qint64(accountId));
        q.addBindValue(qint64(id));
        q.addBindValue(
            channel.favoriteRank ? QVariant(qint64(*channel.favoriteRank)) : QVariant(QMetaType::fromType<qint64>()));
        q.addBindValue(channel.hidden);
        q.addBindValue(channel.collapsed);
        if (!q.exec())
            return failed();
    }
    q.prepare(QStringLiteral("INSERT INTO sidebar_views VALUES(?,?,?) ON CONFLICT(account_id) DO UPDATE SET "
                             "show_voice_participants=excluded.show_voice_participants,compact=excluded.compact"));
    q.addBindValue(qint64(accountId));
    q.addBindValue(preferences.showVoiceParticipants);
    q.addBindValue(preferences.compact);
    if (!q.exec() || !m_db.commit())
        return failed();
    return true;
}

bool LocalStore::clearChannelPreferences(std::int64_t accountId, std::uint64_t channelId)
{
    if (!m_db.transaction())
        return false;
    for (const auto* table : {"channel_preferences", "sidebar_channels"}) {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("DELETE FROM %1 WHERE account_id=? AND channel_id=?").arg(QLatin1StringView(table)));
        q.addBindValue(qint64(accountId));
        q.addBindValue(qint64(channelId));
        if (!q.exec()) {
            m_db.rollback();
            return false;
        }
    }
    if (m_db.commit())
        return true;
    m_db.rollback();
    return false;
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
