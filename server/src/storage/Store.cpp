#include "storage/Store.hpp"
#include "omachat/core/Snowflake.hpp"

#include "omachat/core/Log.hpp"

#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>

namespace omachat::server {
namespace {

constexpr int kSchemaVersion = 14;

const char* const kSchemaV14[] = {
    R"(CREATE TABLE message_operations(
        user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
        operation_id BLOB NOT NULL CHECK(length(operation_id) = 16),
        digest BLOB NOT NULL, result BLOB NOT NULL, message_id INTEGER NOT NULL,
        PRIMARY KEY(user_id, operation_id)))",
    R"(CREATE TABLE read_markers(
        user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
        channel_id INTEGER NOT NULL REFERENCES channels(id) ON DELETE CASCADE,
        message_id INTEGER NOT NULL, timestamp INTEGER NOT NULL,
        PRIMARY KEY(user_id, channel_id)))",
    R"(CREATE TRIGGER message_operation_tombstone AFTER DELETE ON messages BEGIN
        UPDATE message_operations SET result=X'' WHERE message_id=old.id;
    END)"};

const char* const kSchemaV1[] = {
    R"(CREATE TABLE users(
        id INTEGER PRIMARY KEY,
        username TEXT NOT NULL UNIQUE,
        display_name TEXT NOT NULL,
        avatar_url TEXT NOT NULL DEFAULT '',
        password_hash TEXT NOT NULL,
        created_at INTEGER NOT NULL))",
    R"(CREATE TABLE sessions(
        id INTEGER PRIMARY KEY,
        user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
        refresh_digest BLOB NOT NULL UNIQUE,
        created_at INTEGER NOT NULL,
        expires_at INTEGER NOT NULL))",
    R"(CREATE TABLE servers(
        id INTEGER PRIMARY KEY,
        name TEXT NOT NULL,
        icon_url TEXT NOT NULL DEFAULT '',
        owner_id INTEGER NOT NULL REFERENCES users(id),
        created_at INTEGER NOT NULL))",
    R"(CREATE TABLE roles(
        id INTEGER PRIMARY KEY,
        server_id INTEGER NOT NULL REFERENCES servers(id) ON DELETE CASCADE,
        name TEXT NOT NULL,
        permissions INTEGER NOT NULL,
        position INTEGER NOT NULL,
        color INTEGER NOT NULL DEFAULT 0,
        is_default INTEGER NOT NULL DEFAULT 0))",
    R"(CREATE TABLE members(
        server_id INTEGER NOT NULL REFERENCES servers(id) ON DELETE CASCADE,
        user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
        joined_at INTEGER NOT NULL,
        PRIMARY KEY(server_id, user_id)))",
    R"(CREATE TABLE member_roles(
        server_id INTEGER NOT NULL,
        user_id INTEGER NOT NULL,
        role_id INTEGER NOT NULL REFERENCES roles(id) ON DELETE CASCADE,
        PRIMARY KEY(server_id, user_id, role_id),
        FOREIGN KEY(server_id, user_id) REFERENCES members(server_id, user_id) ON DELETE CASCADE))",
    R"(CREATE TABLE channels(
        id INTEGER PRIMARY KEY,
        server_id INTEGER REFERENCES servers(id) ON DELETE CASCADE,
        name TEXT NOT NULL,
        type INTEGER NOT NULL,
        parent_id INTEGER NOT NULL DEFAULT 0,
        position INTEGER NOT NULL DEFAULT 0,
        topic TEXT NOT NULL DEFAULT '',
        created_at INTEGER NOT NULL))",
    R"(CREATE TABLE dm_recipients(
        channel_id INTEGER NOT NULL REFERENCES channels(id) ON DELETE CASCADE,
        user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
        PRIMARY KEY(channel_id, user_id)))",
    R"(CREATE TABLE overrides(
        channel_id INTEGER NOT NULL REFERENCES channels(id) ON DELETE CASCADE,
        target_type INTEGER NOT NULL,
        target_id INTEGER NOT NULL,
        allow INTEGER NOT NULL,
        deny INTEGER NOT NULL,
        PRIMARY KEY(channel_id, target_type, target_id)))",
    R"(CREATE TABLE bans(
        server_id INTEGER NOT NULL REFERENCES servers(id) ON DELETE CASCADE,
        user_id INTEGER NOT NULL,
        banned_by INTEGER NOT NULL,
        reason TEXT NOT NULL DEFAULT '',
        created_at INTEGER NOT NULL,
        PRIMARY KEY(server_id, user_id)))",
    R"(CREATE TABLE invites(
        token TEXT PRIMARY KEY,
        server_id INTEGER NOT NULL REFERENCES servers(id) ON DELETE CASCADE,
        creator_id INTEGER NOT NULL,
        created_at INTEGER NOT NULL,
        expires_at INTEGER NOT NULL,
        max_uses INTEGER NOT NULL,
        uses INTEGER NOT NULL DEFAULT 0))",
    R"(CREATE TABLE messages(
        id INTEGER PRIMARY KEY,
        channel_id INTEGER NOT NULL REFERENCES channels(id) ON DELETE CASCADE,
        author_id INTEGER NOT NULL,
        content TEXT NOT NULL,
        reply_to INTEGER NOT NULL DEFAULT 0,
        edited_at INTEGER NOT NULL DEFAULT 0,
        is_action INTEGER NOT NULL DEFAULT 0,
        mentions TEXT NOT NULL DEFAULT ''))",
    "CREATE INDEX messages_by_channel ON messages(channel_id, id DESC)",
    R"(CREATE TABLE reactions(
        message_id INTEGER NOT NULL REFERENCES messages(id) ON DELETE CASCADE,
        user_id INTEGER NOT NULL,
        emoji TEXT NOT NULL,
        PRIMARY KEY(message_id, user_id, emoji)))",
    // SQLite-specific: FTS5 external-content index for channel search.
    "CREATE VIRTUAL TABLE messages_fts USING fts5(content, content='messages', content_rowid='id')",
    R"(CREATE TRIGGER messages_ai AFTER INSERT ON messages BEGIN
        INSERT INTO messages_fts(rowid, content) VALUES (new.id, new.content);
       END)",
    R"(CREATE TRIGGER messages_ad AFTER DELETE ON messages BEGIN
        INSERT INTO messages_fts(messages_fts, rowid, content) VALUES('delete', old.id, old.content);
       END)",
    R"(CREATE TRIGGER messages_au AFTER UPDATE OF content ON messages BEGIN
        INSERT INTO messages_fts(messages_fts, rowid, content) VALUES('delete', old.id, old.content);
        INSERT INTO messages_fts(rowid, content) VALUES (new.id, new.content);
       END)",
};

// v2: attachments. message_id is NULL while the upload is pending so the
// foreign key can cascade deletes from messages and channels.
const char* const kSchemaV2[] = {
    R"(CREATE TABLE attachments(
        id INTEGER PRIMARY KEY,
        channel_id INTEGER NOT NULL REFERENCES channels(id) ON DELETE CASCADE,
        uploader_id INTEGER NOT NULL,
        message_id INTEGER REFERENCES messages(id) ON DELETE CASCADE,
        filename TEXT NOT NULL,
        mime_type TEXT NOT NULL,
        size INTEGER NOT NULL,
        sha256 BLOB NOT NULL,
        created_at INTEGER NOT NULL))",
    "CREATE INDEX attachments_by_message ON attachments(message_id)",
    "CREATE INDEX attachments_pending ON attachments(uploader_id) WHERE message_id IS NULL",
};

// v3: end-to-end encrypted direct messages. The server stores the opaque
// payload and each user's published device keys, never a private key.
const char* const kSchemaV3[] = {
    "ALTER TABLE messages ADD COLUMN encrypted BLOB",
    R"(CREATE TABLE device_keys(
        user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
        public_key BLOB NOT NULL,
        created_at INTEGER NOT NULL,
        PRIMARY KEY(user_id, public_key)))",
};

// v4: OAuth login (Discord/GitHub/Google). A provider identity maps to
// exactly one local user; password_hash may now be empty for accounts that
// were created via OAuth and never set one.
const char* const kSchemaV4[] = {
    R"(CREATE TABLE oauth_identities(
        id INTEGER PRIMARY KEY,
        user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
        provider TEXT NOT NULL,
        provider_user_id TEXT NOT NULL,
        provider_username TEXT NOT NULL DEFAULT '',
        linked_at INTEGER NOT NULL,
        UNIQUE(provider, provider_user_id)))",
    "CREATE INDEX oauth_identities_by_user ON oauth_identities(user_id)",
};

const char* const kSchemaV5[] = {
    "ALTER TABLE users ADD COLUMN bio TEXT NOT NULL DEFAULT ''",
};

const char* const kSchemaV6[] = {
    "ALTER TABLE messages ADD COLUMN created_at INTEGER NOT NULL DEFAULT 0",
    // Existing message ids encode their creation time. Imported history can
    // predate the OmaChat snowflake epoch, so its time must live separately.
    "UPDATE messages SET created_at = 1735689600000 + (id >> 22)",
    "CREATE INDEX messages_by_channel_time ON messages(channel_id, created_at DESC, id DESC)",
    R"(CREATE TABLE discord_import_map(
        server_id INTEGER NOT NULL REFERENCES servers(id) ON DELETE CASCADE,
        kind TEXT NOT NULL,
        discord_id TEXT NOT NULL,
        local_id INTEGER NOT NULL,
        PRIMARY KEY(server_id, kind, discord_id),
        UNIQUE(server_id, kind, local_id)))",
    R"(CREATE TABLE discord_import_replies(
        server_id INTEGER NOT NULL REFERENCES servers(id) ON DELETE CASCADE,
        message_id INTEGER NOT NULL REFERENCES messages(id) ON DELETE CASCADE,
        discord_reply_id TEXT NOT NULL,
        PRIMARY KEY(server_id, message_id)))",
};

// v7: custom per-server emoji. The image itself is an ordinary attachment
// (reuses the existing upload pipeline); this table just names it.
const char* const kSchemaV7[] = {
    R"(CREATE TABLE emoji(
        id INTEGER PRIMARY KEY,
        server_id INTEGER NOT NULL REFERENCES servers(id) ON DELETE CASCADE,
        name TEXT NOT NULL,
        attachment_id INTEGER NOT NULL REFERENCES attachments(id) ON DELETE CASCADE,
        uploader_id INTEGER NOT NULL,
        created_at INTEGER NOT NULL,
        UNIQUE(server_id, name)))",
    "CREATE INDEX emoji_by_server ON emoji(server_id)",
};

const char* const kSchemaV8[] = {
    "ALTER TABLE channels ADD COLUMN description TEXT NOT NULL DEFAULT ''",
};

const char* const kSchemaV9[] = {
    "ALTER TABLE channels ADD COLUMN icon_attachment_id INTEGER NOT NULL DEFAULT 0",
    "ALTER TABLE channels ADD COLUMN banner_attachment_id INTEGER NOT NULL DEFAULT 0",
};

const char* const kSchemaV10[] = {
    "ALTER TABLE attachments ADD COLUMN artwork INTEGER NOT NULL DEFAULT 0",
};

const char* const kSchemaV11[] = {
    "CREATE TABLE instance_settings(key TEXT PRIMARY KEY, value TEXT NOT NULL)",
};
const char* const kSchemaV12[] = {
    "CREATE TABLE instance_suspensions(user_id INTEGER PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE)",
    "CREATE TABLE instance_audit(id INTEGER PRIMARY KEY AUTOINCREMENT, at INTEGER NOT NULL, actor_id INTEGER NOT NULL, "
    "action TEXT NOT NULL, target_id INTEGER NOT NULL)",
    "CREATE INDEX instance_audit_recent ON instance_audit(id DESC)",
};

// v13: server identity (description plus icon/banner artwork, like channels).
const char* const kSchemaV13[] = {
    "ALTER TABLE servers ADD COLUMN description TEXT NOT NULL DEFAULT ''",
    "ALTER TABLE servers ADD COLUMN icon_attachment_id INTEGER NOT NULL DEFAULT 0",
    "ALTER TABLE servers ADD COLUMN banner_attachment_id INTEGER NOT NULL DEFAULT 0",
};

qint64 sid(Id id)
{
    return static_cast<qint64>(id);
}
Id uid(const QVariant& v)
{
    return static_cast<Id>(v.toLongLong());
}

QString joinIds(const std::vector<Id>& ids)
{
    QStringList parts;
    parts.reserve(static_cast<qsizetype>(ids.size()));
    for (Id id : ids)
        parts << QString::number(id);
    return parts.join(u' ');
}

std::vector<Id> splitIds(const QString& s)
{
    std::vector<Id> out;
    for (const auto& part : s.split(u' ', Qt::SkipEmptyParts))
        out.push_back(part.toULongLong());
    return out;
}

MessageRecord readMessage(const QSqlQuery& q)
{
    MessageRecord m;
    m.id = uid(q.value(0));
    m.channelId = uid(q.value(1));
    m.authorId = uid(q.value(2));
    m.content = q.value(3).toString();
    m.replyTo = uid(q.value(4));
    m.editedAt = q.value(5).toLongLong();
    m.isAction = q.value(6).toBool();
    m.mentions = splitIds(q.value(7).toString());
    m.encrypted = q.value(8).toByteArray();
    m.createdAt = q.value(9).toLongLong();
    return m;
}

AttachmentRecord readAttachment(const QSqlQuery& q)
{
    AttachmentRecord a;
    a.id = uid(q.value(0));
    a.channelId = uid(q.value(1));
    a.uploaderId = uid(q.value(2));
    a.messageId = q.value(3).isNull() ? 0 : uid(q.value(3));
    a.filename = q.value(4).toString();
    a.mimeType = q.value(5).toString();
    a.size = static_cast<std::uint64_t>(q.value(6).toLongLong());
    a.sha256 = q.value(7).toByteArray();
    a.createdAt = q.value(8).toLongLong();
    a.artwork = q.value(9).toBool();
    return a;
}

constexpr const char* kAttachmentColumns
    = "id, channel_id, uploader_id, message_id, filename, mime_type, size, sha256, created_at, artwork";

constexpr const char* kMessageColumns
    = "id, channel_id, author_id, content, reply_to, edited_at, is_action, mentions, encrypted, created_at";

} // namespace

Store::Store()
    : m_connectionName(QUuid::createUuid().toString())
{
}

Store::~Store()
{
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool Store::open(const QString& path, QString* error)
{
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(path);
    if (!m_db.open()) {
        if (error)
            *error = m_db.lastError().text();
        return false;
    }
    exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    exec(QStringLiteral("PRAGMA busy_timeout=5000"));
    return migrate(error);
}

int Store::schemaVersion() const
{
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("PRAGMA user_version")) && q.next())
        return q.value(0).toInt();
    return 0;
}

QString Store::instanceId()
{
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT value FROM instance_settings WHERE key='instance_id'")))
        return {};
    if (q.next())
        return q.value(0).toString();
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!exec(QStringLiteral("INSERT INTO instance_settings(key,value) VALUES('instance_id',?)"), {id}))
        return {};
    return id;
}

std::optional<bool> Store::registrationOverride()
{
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT value FROM instance_settings WHERE key = 'registration_open'")) && q.next())
        return q.value(0).toString() == u"1";
    return std::nullopt;
}

bool Store::setRegistrationOverride(bool open)
{
    return exec(QStringLiteral("INSERT INTO instance_settings(key, value) VALUES('registration_open', ?) "
                               "ON CONFLICT(key) DO UPDATE SET value = excluded.value"),
        {open ? QStringLiteral("1") : QStringLiteral("0")});
}

std::uint64_t Store::messageCount()
{
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM messages")) && q.next())
        return q.value(0).toULongLong();
    return 0;
}

bool Store::isSuspended(Id userId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT 1 FROM instance_suspensions WHERE user_id = ?"));
    q.addBindValue(sid(userId));
    return q.exec() && q.next();
}

bool Store::setSuspended(Id userId, bool suspended)
{
    return suspended
        ? exec(QStringLiteral("INSERT OR IGNORE INTO instance_suspensions(user_id) VALUES(?)"), {sid(userId)})
        : exec(QStringLiteral("DELETE FROM instance_suspensions WHERE user_id = ?"), {sid(userId)});
}

bool Store::deleteSessionsForUser(Id userId)
{
    return exec(QStringLiteral("DELETE FROM sessions WHERE user_id = ?"), {sid(userId)});
}

bool Store::recordAudit(std::int64_t at, Id actorId, const QString& action, Id targetId)
{
    return exec(QStringLiteral("INSERT INTO instance_audit(at, actor_id, action, target_id) VALUES(?, ?, ?, ?)"),
        {qint64(at), sid(actorId), action, sid(targetId)});
}

std::vector<Store::AuditEntry> Store::recentAudit()
{
    std::vector<AuditEntry> result;
    QSqlQuery q(m_db);
    if (!q.exec(
            QStringLiteral("SELECT at, actor_id, action, target_id FROM instance_audit ORDER BY id DESC LIMIT 100")))
        return result;
    while (q.next())
        result.push_back({q.value(0).toLongLong(), uid(q.value(1)), q.value(2).toString(), uid(q.value(3))});
    return result;
}

bool Store::migrate(QString* error)
{
    const int current = schemaVersion();
    if (current > kSchemaVersion) {
        if (error)
            *error
                = QStringLiteral("database schema %1 is newer than this server (%2)").arg(current).arg(kSchemaVersion);
        return false;
    }
    if (current == kSchemaVersion)
        return true;

    m_db.transaction();
    const auto apply = [&](const auto& statements) {
        for (const char* stmt : statements) {
            QSqlQuery q(m_db);
            if (!q.exec(QString::fromUtf8(stmt))) {
                if (error)
                    *error = q.lastError().text();
                m_db.rollback();
                return false;
            }
        }
        return true;
    };
    if (current < 1 && !apply(kSchemaV1))
        return false;
    if (current < 2 && !apply(kSchemaV2))
        return false;
    if (current < 3 && !apply(kSchemaV3))
        return false;
    if (current < 4 && !apply(kSchemaV4))
        return false;
    if (current < 5 && !apply(kSchemaV5))
        return false;
    if (current < 6 && !apply(kSchemaV6))
        return false;
    if (current < 7 && !apply(kSchemaV7))
        return false;
    if (current < 8 && !apply(kSchemaV8))
        return false;
    if (current < 9 && !apply(kSchemaV9))
        return false;
    if (current < 10 && !apply(kSchemaV10))
        return false;
    if (current < 11 && !apply(kSchemaV11))
        return false;
    if (current < 12 && !apply(kSchemaV12))
        return false;
    if (current < 13 && !apply(kSchemaV13))
        return false;
    if (current < 14 && !apply(kSchemaV14))
        return false;
    exec(QStringLiteral("PRAGMA user_version=%1").arg(kSchemaVersion));
    if (!m_db.commit()) {
        if (error)
            *error = m_db.lastError().text();
        return false;
    }
    OMA_INFO("storage", "database migrated", {"from", current}, {"to", kSchemaVersion});
    return true;
}

bool Store::exec(const QString& sql, const std::vector<QVariant>& binds)
{
    QSqlQuery q(m_db);
    if (!q.prepare(sql)) {
        OMA_ERROR("storage", "prepare failed", {"error", q.lastError().text()});
        return false;
    }
    for (const auto& b : binds) {
        // A null QString binds as SQL NULL; OmaChat text columns are NOT NULL
        // and use '' for "empty".
        if (b.typeId() == QMetaType::QString && b.toString().isNull())
            q.addBindValue(QStringLiteral(""));
        else
            q.addBindValue(b);
    }
    if (!q.exec()) {
        // Constraint violations (e.g. a username race) are expected; anything
        // else is worth seeing in the logs.
        const auto err = q.lastError();
        if (err.nativeErrorCode() == QLatin1StringView("2067") || err.nativeErrorCode() == QLatin1StringView("1555"))
            OMA_DEBUG("storage", "constraint violation", {"error", err.text()});
        else
            OMA_WARN("storage", "statement failed", {"error", err.text()});
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- users

bool Store::insertUser(const UserRecord& u)
{
    return exec(QStringLiteral("INSERT INTO users(id, username, display_name, avatar_url, password_hash, "
                               "created_at) VALUES(?,?,?,?,?,?)"),
        {sid(u.id), u.username, u.displayName, u.avatarUrl, u.passwordHash, qint64(u.createdAt)});
}

bool Store::deleteUser(Id userId)
{
    if (!begin())
        return false;
    const auto fail = [this] {
        rollback();
        return false;
    };
    // An owner cannot be removed while their servers still reference them.
    if (!exec(QStringLiteral("DELETE FROM servers WHERE owner_id = ?"), {sid(userId)})
        || !exec(QStringLiteral("DELETE FROM channels WHERE server_id IS NULL AND id IN "
                                "(SELECT channel_id FROM dm_recipients WHERE user_id = ?)"),
            {sid(userId)})
        || !exec(QStringLiteral("DELETE FROM bans WHERE user_id = ?"), {sid(userId)})
        || !exec(QStringLiteral("DELETE FROM overrides WHERE target_type = 1 AND target_id = ?"), {sid(userId)})
        || !exec(QStringLiteral("DELETE FROM reactions WHERE user_id = ?"), {sid(userId)})
        || !exec(QStringLiteral("DELETE FROM attachments WHERE uploader_id = ? AND message_id IS NULL "
                                "AND id NOT IN (SELECT attachment_id FROM emoji) "
                                "AND id NOT IN (SELECT icon_attachment_id FROM channels) "
                                "AND id NOT IN (SELECT banner_attachment_id FROM channels) "
                                "AND id NOT IN (SELECT icon_attachment_id FROM servers) "
                                "AND id NOT IN (SELECT banner_attachment_id FROM servers)"),
            {sid(userId)})
        || !exec(QStringLiteral("DELETE FROM users WHERE id = ?"), {sid(userId)}))
        return fail();
    if (!commit())
        return fail();
    return true;
}

std::optional<UserRecord> Store::userByName(const QString& username)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, username, display_name, avatar_url, password_hash, created_at, bio "
                             "FROM users WHERE username = ?"));
    q.addBindValue(username);
    if (!q.exec() || !q.next())
        return std::nullopt;
    return UserRecord{uid(q.value(0)), q.value(1).toString(), q.value(2).toString(), q.value(3).toString(),
        q.value(4).toString(), q.value(5).toLongLong(), q.value(6).toString()};
}

std::vector<UserRecord> Store::allUsers()
{
    std::vector<UserRecord> out;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT id, username, display_name, avatar_url, created_at, bio FROM users"));
    while (q.next()) {
        out.push_back(UserRecord{uid(q.value(0)), q.value(1).toString(), q.value(2).toString(), q.value(3).toString(),
            QString(), q.value(4).toLongLong(), q.value(5).toString()});
    }
    return out;
}

bool Store::updateUserPasswordHash(Id userId, const QString& hash)
{
    return exec(QStringLiteral("UPDATE users SET password_hash = ? WHERE id = ?"), {hash, sid(userId)});
}

bool Store::updateUserProfile(Id userId, const QString& displayName, const QString& avatarUrl, const QString& bio)
{
    return exec(QStringLiteral("UPDATE users SET display_name = ?, avatar_url = ?, bio = ? WHERE id = ?"),
        {displayName, avatarUrl, bio, sid(userId)});
}

// ------------------------------------------------------------ oauth

bool Store::insertOAuthIdentity(const OAuthIdentityRecord& identity)
{
    return exec(QStringLiteral("INSERT INTO oauth_identities(id, user_id, provider, provider_user_id, "
                               "provider_username, linked_at) VALUES(?,?,?,?,?,?)"),
        {sid(identity.id), sid(identity.userId), identity.provider, identity.providerUserId, identity.providerUsername,
            qint64(identity.linkedAt)});
}

std::optional<OAuthIdentityRecord> Store::oauthIdentity(const QString& provider, const QString& providerUserId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, user_id, provider, provider_user_id, provider_username, linked_at "
                             "FROM oauth_identities WHERE provider = ? AND provider_user_id = ?"));
    q.addBindValue(provider);
    q.addBindValue(providerUserId);
    if (!q.exec() || !q.next())
        return std::nullopt;
    return OAuthIdentityRecord{uid(q.value(0)), uid(q.value(1)), q.value(2).toString(), q.value(3).toString(),
        q.value(4).toString(), q.value(5).toLongLong()};
}

bool Store::updateOAuthIdentityUsername(Id identityId, const QString& providerUsername)
{
    return exec(QStringLiteral("UPDATE oauth_identities SET provider_username = ? WHERE id = ?"),
        {providerUsername, sid(identityId)});
}

std::vector<OAuthIdentityRecord> Store::oauthIdentitiesForUser(Id userId)
{
    std::vector<OAuthIdentityRecord> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, user_id, provider, provider_user_id, provider_username, linked_at "
                             "FROM oauth_identities WHERE user_id = ?"));
    q.addBindValue(sid(userId));
    if (!q.exec())
        return out;
    while (q.next()) {
        out.push_back(OAuthIdentityRecord{uid(q.value(0)), uid(q.value(1)), q.value(2).toString(),
            q.value(3).toString(), q.value(4).toString(), q.value(5).toLongLong()});
    }
    return out;
}

bool Store::deleteOAuthIdentity(Id userId, const QString& provider)
{
    return exec(
        QStringLiteral("DELETE FROM oauth_identities WHERE user_id = ? AND provider = ?"), {sid(userId), provider});
}

bool Store::hasPassword(Id userId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT password_hash FROM users WHERE id = ?"));
    q.addBindValue(sid(userId));
    return q.exec() && q.next() && !q.value(0).toString().isEmpty();
}

// ------------------------------------------------------------- sessions

bool Store::insertSession(const SessionRecord& s)
{
    return exec(QStringLiteral("INSERT INTO sessions(id, user_id, refresh_digest, created_at, expires_at) "
                               "VALUES(?,?,?,strftime('%s','now')*1000,?)"),
        {sid(s.id), sid(s.userId), s.refreshDigest, qint64(s.expiresAt)});
}

std::optional<SessionRecord> Store::sessionByDigest(const QByteArray& digest)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, user_id, refresh_digest, expires_at FROM sessions "
                             "WHERE refresh_digest = ?"));
    q.addBindValue(digest);
    if (!q.exec() || !q.next())
        return std::nullopt;
    return SessionRecord{uid(q.value(0)), uid(q.value(1)), q.value(2).toByteArray(), q.value(3).toLongLong()};
}

bool Store::rotateSession(Id sessionId, const QByteArray& newDigest, std::int64_t expiresAt)
{
    return exec(QStringLiteral("UPDATE sessions SET refresh_digest = ?, expires_at = ? WHERE id = ?"),
        {newDigest, qint64(expiresAt), sid(sessionId)});
}

bool Store::deleteSession(Id sessionId)
{
    return exec(QStringLiteral("DELETE FROM sessions WHERE id = ?"), {sid(sessionId)});
}

bool Store::sessionActive(Id sessionId, Id userId, std::int64_t nowMs)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT 1 FROM sessions WHERE id = ? AND user_id = ? AND expires_at >= ?"));
    q.addBindValue(sid(sessionId));
    q.addBindValue(sid(userId));
    q.addBindValue(qint64(nowMs));
    return q.exec() && q.next();
}

std::optional<std::vector<SessionRecord>> Store::loginSessions(Id userId, Id beforeId, std::int64_t nowMs)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, created_at, expires_at FROM sessions WHERE user_id = ? "
                             "AND expires_at >= ? AND (? = 0 OR id < ?) ORDER BY id DESC LIMIT 101"));
    q.addBindValue(sid(userId));
    q.addBindValue(qint64(nowMs));
    q.addBindValue(sid(beforeId));
    q.addBindValue(sid(beforeId));
    if (!q.exec())
        return std::nullopt;
    std::vector<SessionRecord> rows;
    while (q.next())
        rows.push_back({uid(q.value(0)), userId, {}, q.value(2).toLongLong(), q.value(1).toLongLong()});
    return rows;
}

std::optional<bool> Store::revokeLoginSession(Id userId, Id sessionId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM sessions WHERE user_id = ? AND id = ?"));
    q.addBindValue(sid(userId));
    q.addBindValue(sid(sessionId));
    if (!q.exec())
        return std::nullopt;
    return q.numRowsAffected() == 1;
}

int Store::purgeExpiredSessions(std::int64_t nowMs)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM sessions WHERE expires_at < ?"));
    q.addBindValue(qint64(nowMs));
    q.exec();
    return q.numRowsAffected();
}

// ------------------------------------------------------------- snapshot

Store::Snapshot Store::loadSnapshot()
{
    Snapshot snap;
    std::map<Id, ServerRecord> servers;
    {
        QSqlQuery q(m_db);
        q.exec(QStringLiteral("SELECT id, name, icon_url, owner_id, description, icon_attachment_id, "
                              "banner_attachment_id FROM servers"));
        while (q.next()) {
            ServerRecord s;
            s.id = uid(q.value(0));
            s.name = q.value(1).toString();
            s.iconUrl = q.value(2).toString();
            s.ownerId = uid(q.value(3));
            s.description = q.value(4).toString();
            s.iconAttachmentId = uid(q.value(5));
            s.bannerAttachmentId = uid(q.value(6));
            servers.emplace(s.id, std::move(s));
        }
    }
    {
        QSqlQuery q(m_db);
        q.exec(QStringLiteral("SELECT id, server_id, name, permissions, position, color, is_default FROM roles"));
        while (q.next()) {
            RoleRecord r{uid(q.value(0)), uid(q.value(1)), q.value(2).toString(),
                static_cast<std::uint64_t>(q.value(3).toLongLong()), q.value(4).toUInt(), q.value(5).toUInt(),
                q.value(6).toBool()};
            auto it = servers.find(r.serverId);
            if (it == servers.end())
                continue;
            if (r.isDefault)
                it->second.defaultRoleId = r.id;
            it->second.roles.emplace(r.id, std::move(r));
        }
    }
    {
        QSqlQuery q(m_db);
        q.exec(QStringLiteral("SELECT server_id, user_id, joined_at FROM members"));
        while (q.next()) {
            MemberRecord m{uid(q.value(0)), uid(q.value(1)), q.value(2).toLongLong(), {}};
            auto it = servers.find(m.serverId);
            if (it != servers.end())
                it->second.members.emplace(m.userId, std::move(m));
        }
    }
    {
        QSqlQuery q(m_db);
        q.exec(QStringLiteral("SELECT server_id, user_id, role_id FROM member_roles"));
        while (q.next()) {
            auto it = servers.find(uid(q.value(0)));
            if (it == servers.end())
                continue;
            auto mit = it->second.members.find(uid(q.value(1)));
            if (mit != it->second.members.end())
                mit->second.roles.insert(uid(q.value(2)));
        }
    }
    {
        QSqlQuery q(m_db);
        q.exec(QStringLiteral("SELECT server_id, user_id FROM bans"));
        while (q.next()) {
            auto it = servers.find(uid(q.value(0)));
            if (it != servers.end())
                it->second.bans.insert(uid(q.value(1)));
        }
    }
    std::map<Id, ChannelRecord> channels;
    {
        QSqlQuery q(m_db);
        q.exec(QStringLiteral("SELECT id, COALESCE(server_id, 0), name, type, parent_id, position, topic, description, "
                              "icon_attachment_id, banner_attachment_id FROM channels"));
        while (q.next()) {
            ChannelRecord c;
            c.id = uid(q.value(0));
            c.serverId = uid(q.value(1));
            c.name = q.value(2).toString();
            c.kind = static_cast<ChannelKind>(q.value(3).toInt());
            c.parentId = uid(q.value(4));
            c.position = q.value(5).toUInt();
            c.topic = q.value(6).toString();
            c.description = q.value(7).toString();
            c.iconAttachmentId = uid(q.value(8));
            c.bannerAttachmentId = uid(q.value(9));
            if (c.serverId) {
                auto it = servers.find(c.serverId);
                if (it != servers.end())
                    it->second.channels.insert(c.id);
            }
            channels.emplace(c.id, std::move(c));
        }
    }
    {
        QSqlQuery q(m_db);
        q.exec(QStringLiteral("SELECT channel_id, user_id FROM dm_recipients ORDER BY user_id"));
        while (q.next()) {
            auto it = channels.find(uid(q.value(0)));
            if (it != channels.end())
                it->second.recipients.push_back(uid(q.value(1)));
        }
    }
    {
        QSqlQuery q(m_db);
        q.exec(QStringLiteral("SELECT channel_id, target_type, target_id, allow, deny FROM overrides"));
        while (q.next()) {
            snap.overrides.push_back(OverrideRecord{uid(q.value(0)), q.value(1).toInt(), uid(q.value(2)),
                static_cast<std::uint64_t>(q.value(3).toLongLong()),
                static_cast<std::uint64_t>(q.value(4).toLongLong())});
        }
    }
    for (auto& [id, s] : servers)
        snap.servers.push_back(std::move(s));
    for (auto& [id, c] : channels)
        snap.channels.push_back(std::move(c));
    return snap;
}

bool Store::insertServer(const ServerRecord& s, std::int64_t createdAt)
{
    return exec(QStringLiteral("INSERT INTO servers(id, name, icon_url, owner_id, created_at, description, "
                               "icon_attachment_id, banner_attachment_id) VALUES(?,?,?,?,?,?,?,?)"),
        {sid(s.id), s.name, s.iconUrl, sid(s.ownerId), qint64(createdAt), s.description, sid(s.iconAttachmentId),
            sid(s.bannerAttachmentId)});
}

bool Store::updateServer(const ServerRecord& s)
{
    return exec(QStringLiteral("UPDATE servers SET name = ?, description = ?, icon_attachment_id = ?, "
                               "banner_attachment_id = ? WHERE id = ?"),
        {s.name, s.description, sid(s.iconAttachmentId), sid(s.bannerAttachmentId), sid(s.id)});
}

bool Store::deleteServer(Id id)
{
    return exec(QStringLiteral("DELETE FROM servers WHERE id = ?"), {sid(id)});
}

bool Store::insertRole(const RoleRecord& r)
{
    return exec(QStringLiteral("INSERT INTO roles(id, server_id, name, permissions, position, color, is_default) "
                               "VALUES(?,?,?,?,?,?,?)"),
        {sid(r.id), sid(r.serverId), r.name, static_cast<qint64>(r.permissions), r.position, r.color,
            r.isDefault ? 1 : 0});
}

bool Store::updateRole(const RoleRecord& r)
{
    return exec(QStringLiteral("UPDATE roles SET name = ?, permissions = ?, position = ?, color = ? WHERE id = ?"),
        {r.name, static_cast<qint64>(r.permissions), r.position, r.color, sid(r.id)});
}

bool Store::deleteRole(Id id)
{
    return exec(QStringLiteral("DELETE FROM roles WHERE id = ?"), {sid(id)});
}

bool Store::insertMember(const MemberRecord& m)
{
    if (!exec(QStringLiteral("INSERT INTO members(server_id, user_id, joined_at) VALUES(?,?,?)"),
            {sid(m.serverId), sid(m.userId), qint64(m.joinedAt)}))
        return false;
    for (Id role : m.roles)
        setMemberRole(m.serverId, m.userId, role, true);
    return true;
}

bool Store::deleteMember(Id serverId, Id userId)
{
    return exec(
        QStringLiteral("DELETE FROM members WHERE server_id = ? AND user_id = ?"), {sid(serverId), sid(userId)});
}

bool Store::setMemberRole(Id serverId, Id userId, Id roleId, bool add)
{
    if (add)
        return exec(QStringLiteral("INSERT OR IGNORE INTO member_roles(server_id, user_id, role_id) VALUES(?,?,?)"),
            {sid(serverId), sid(userId), sid(roleId)});
    return exec(QStringLiteral("DELETE FROM member_roles WHERE server_id = ? AND user_id = ? AND role_id = ?"),
        {sid(serverId), sid(userId), sid(roleId)});
}

bool Store::insertChannel(const ChannelRecord& c, std::int64_t createdAt)
{
    const QVariant server = c.serverId ? QVariant(sid(c.serverId)) : QVariant(QMetaType(QMetaType::LongLong));
    if (!exec(QStringLiteral("INSERT INTO channels(id, server_id, name, type, parent_id, position, topic, created_at, "
                             "description, icon_attachment_id, banner_attachment_id) VALUES(?,?,?,?,?,?,?,?,?,?,?)"),
            {sid(c.id), server, c.name, static_cast<int>(c.kind), sid(c.parentId), c.position, c.topic,
                qint64(createdAt), c.description, sid(c.iconAttachmentId), sid(c.bannerAttachmentId)}))
        return false;
    for (Id r : c.recipients)
        exec(QStringLiteral("INSERT INTO dm_recipients(channel_id, user_id) VALUES(?,?)"), {sid(c.id), sid(r)});
    return true;
}

bool Store::updateChannel(const ChannelRecord& c)
{
    return exec(QStringLiteral("UPDATE channels SET name = ?, parent_id = ?, position = ?, topic = ?, description = ?, "
                               "icon_attachment_id = ?, banner_attachment_id = ? WHERE id = ?"),
        {c.name, sid(c.parentId), c.position, c.topic, c.description, sid(c.iconAttachmentId),
            sid(c.bannerAttachmentId), sid(c.id)});
}

bool Store::setRecipient(Id channelId, Id userId, bool present)
{
    return present ? exec(QStringLiteral("INSERT OR IGNORE INTO dm_recipients(channel_id, user_id) VALUES(?,?)"),
                         {sid(channelId), sid(userId)})
                   : exec(QStringLiteral("DELETE FROM dm_recipients WHERE channel_id = ? AND user_id = ?"),
                         {sid(channelId), sid(userId)});
}

bool Store::deleteChannel(Id id)
{
    return exec(QStringLiteral("DELETE FROM channels WHERE id = ?"), {sid(id)});
}

bool Store::upsertOverride(const OverrideRecord& o)
{
    return exec(QStringLiteral("INSERT INTO overrides(channel_id, target_type, target_id, allow, deny) "
                               "VALUES(?,?,?,?,?) ON CONFLICT(channel_id, target_type, target_id) "
                               "DO UPDATE SET allow = excluded.allow, deny = excluded.deny"),
        {sid(o.channelId), o.targetType, sid(o.targetId), static_cast<qint64>(o.allow), static_cast<qint64>(o.deny)});
}

bool Store::deleteOverride(Id channelId, int targetType, Id targetId)
{
    return exec(QStringLiteral("DELETE FROM overrides WHERE channel_id = ? AND target_type = ? AND target_id = ?"),
        {sid(channelId), targetType, sid(targetId)});
}

bool Store::insertBan(Id serverId, Id userId, Id bannedBy, const QString& reason, std::int64_t at)
{
    return exec(QStringLiteral("INSERT OR REPLACE INTO bans(server_id, user_id, banned_by, reason, created_at) "
                               "VALUES(?,?,?,?,?)"),
        {sid(serverId), sid(userId), sid(bannedBy), reason, qint64(at)});
}

bool Store::deleteBan(Id serverId, Id userId)
{
    return exec(QStringLiteral("DELETE FROM bans WHERE server_id = ? AND user_id = ?"), {sid(serverId), sid(userId)});
}

// -------------------------------------------------------------- invites

bool Store::insertInvite(const InviteRecord& i)
{
    return exec(
        QStringLiteral("INSERT INTO invites(token, server_id, creator_id, created_at, expires_at, max_uses, uses) "
                       "VALUES(?,?,?,?,?,?,0)"),
        {i.token, sid(i.serverId), sid(i.creatorId), qint64(i.createdAt), qint64(i.expiresAt), i.maxUses});
}

std::optional<InviteRecord> Store::inviteByToken(const QString& token)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT token, server_id, creator_id, created_at, expires_at, max_uses, uses "
                             "FROM invites WHERE token = ?"));
    q.addBindValue(token);
    if (!q.exec() || !q.next())
        return std::nullopt;
    return InviteRecord{q.value(0).toString(), uid(q.value(1)), uid(q.value(2)), q.value(3).toLongLong(),
        q.value(4).toLongLong(), q.value(5).toUInt(), q.value(6).toUInt()};
}

bool Store::consumeInvite(const QString& token)
{
    QSqlQuery q(m_db);
    q.prepare(
        QStringLiteral("UPDATE invites SET uses = uses + 1 WHERE token = ? AND (max_uses = 0 OR uses < max_uses)"));
    q.addBindValue(token);
    return q.exec() && q.numRowsAffected() == 1;
}

std::vector<InviteRecord> Store::invitesForServer(Id serverId)
{
    std::vector<InviteRecord> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT token, server_id, creator_id, created_at, expires_at, max_uses, uses "
                             "FROM invites WHERE server_id = ? ORDER BY created_at DESC LIMIT 100"));
    q.addBindValue(sid(serverId));
    q.exec();
    while (q.next()) {
        out.push_back(InviteRecord{q.value(0).toString(), uid(q.value(1)), uid(q.value(2)), q.value(3).toLongLong(),
            q.value(4).toLongLong(), q.value(5).toUInt(), q.value(6).toUInt()});
    }
    return out;
}

// ------------------------------------------------------------- messages

std::optional<Id> Store::discordImportId(Id serverId, const QString& kind, const QString& discordId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT local_id FROM discord_import_map WHERE server_id=? AND kind=? AND discord_id=?"));
    q.addBindValue(sid(serverId));
    q.addBindValue(kind);
    q.addBindValue(discordId);
    if (!q.exec() || !q.next())
        return std::nullopt;
    return uid(q.value(0));
}

bool Store::rememberDiscordImport(Id serverId, const QString& kind, const QString& discordId, Id localId)
{
    return exec(QStringLiteral("INSERT INTO discord_import_map(server_id,kind,discord_id,local_id) "
                               "VALUES(?,?,?,?)"),
        {sid(serverId), kind, discordId, sid(localId)});
}

bool Store::rememberDiscordReply(Id serverId, Id messageId, const QString& replyDiscordId)
{
    return exec(QStringLiteral("INSERT OR REPLACE INTO discord_import_replies(server_id,message_id,discord_reply_id) "
                               "VALUES(?,?,?)"),
        {sid(serverId), sid(messageId), replyDiscordId});
}

bool Store::resolveDiscordReplies(Id serverId)
{
    // Keep unresolved references for a later channel batch.
    return exec(QStringLiteral("UPDATE messages SET reply_to = "
                               "(SELECT m.local_id FROM discord_import_replies r "
                               "JOIN discord_import_map m ON m.server_id=r.server_id AND m.kind='message' "
                               "AND m.discord_id=r.discord_reply_id WHERE r.message_id=messages.id) "
                               "WHERE id IN (SELECT r.message_id FROM discord_import_replies r "
                               "JOIN discord_import_map m ON m.server_id=r.server_id AND m.kind='message' "
                               "AND m.discord_id=r.discord_reply_id WHERE r.server_id=?)"),
        {sid(serverId)});
}

bool Store::insertMessage(const MessageRecord& m, Id importServerId, const QString& discordId,
    const QByteArray& operationId, const QByteArray& digest, const QByteArray& result)
{
    if (!begin())
        return false;
    bool ok = exec(QStringLiteral("INSERT INTO messages(id, channel_id, author_id, content, reply_to, edited_at, "
                                  "is_action, mentions, encrypted, created_at) VALUES(?,?,?,?,?,?,?,?,?,?)"),
        {sid(m.id), sid(m.channelId), sid(m.authorId), m.content, sid(m.replyTo), qint64(m.editedAt),
            m.isAction ? 1 : 0, joinIds(m.mentions),
            m.encrypted.isEmpty() ? QVariant(QMetaType(QMetaType::QByteArray)) : QVariant(m.encrypted),
            qint64(m.createdAt ? m.createdAt : decodeSnowflake(m.id).unixMs)});
    for (const auto& a : m.attachments) {
        if (!ok)
            break;
        // Only a pending attachment of this author in this channel can be claimed.
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("UPDATE attachments SET message_id = ? WHERE id = ? AND uploader_id = ? AND "
                                 "channel_id = ? AND message_id IS NULL AND artwork = 0"));
        q.addBindValue(sid(m.id));
        q.addBindValue(sid(a.id));
        q.addBindValue(sid(m.authorId));
        q.addBindValue(sid(m.channelId));
        ok = q.exec() && q.numRowsAffected() == 1;
    }
    if (ok && !operationId.isEmpty())
        ok = exec(QStringLiteral("INSERT INTO message_operations(user_id, operation_id, digest, result, message_id) "
                                 "VALUES(?,?,?,?,?)"),
            {sid(m.authorId), operationId, digest, result, sid(m.id)});
    if (ok && importServerId)
        ok = rememberDiscordImport(importServerId, QStringLiteral("message"), discordId, m.id);
    if (!ok) {
        rollback();
        return false;
    }
    return commit();
}

std::optional<Store::MessageOperation> Store::messageOperation(Id userId, const QByteArray& operationId)
{
    QSqlQuery q(m_db);
    q.prepare(
        QStringLiteral("SELECT digest, result, message_id FROM message_operations WHERE user_id=? AND operation_id=?"));
    q.addBindValue(sid(userId));
    q.addBindValue(operationId);
    if (!q.exec() || !q.next())
        return std::nullopt;
    return MessageOperation{q.value(0).toByteArray(), q.value(1).toByteArray(), uid(q.value(2))};
}

bool Store::setReadMarker(Id userId, const MessageRecord& message)
{
    const auto timestamp = message.createdAt ? message.createdAt : decodeSnowflake(message.id).unixMs;
    return exec(QStringLiteral("INSERT INTO read_markers(user_id,channel_id,message_id,timestamp) VALUES(?,?,?,?) "
                               "ON CONFLICT(user_id,channel_id) DO UPDATE SET message_id=excluded.message_id, "
                               "timestamp=excluded.timestamp WHERE (excluded.timestamp,excluded.message_id) > "
                               "(read_markers.timestamp,read_markers.message_id)"),
        {sid(userId), sid(message.channelId), sid(message.id), qint64(timestamp)});
}

std::vector<proto::ReadMarker> Store::readMarkers(Id userId)
{
    std::vector<proto::ReadMarker> result;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT channel_id,message_id,timestamp FROM read_markers WHERE user_id=?"));
    q.addBindValue(sid(userId));
    if (!q.exec())
        return result;
    while (q.next()) {
        proto::ReadMarker marker;
        marker.set_channel_id(uid(q.value(0)));
        marker.set_message_id(uid(q.value(1)));
        marker.set_timestamp(q.value(2).toLongLong());
        result.push_back(marker);
    }
    return result;
}

std::optional<MessageRecord> Store::message(Id id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM messages WHERE id = ?").arg(QLatin1StringView(kMessageColumns)));
    q.addBindValue(sid(id));
    if (!q.exec() || !q.next())
        return std::nullopt;
    auto m = readMessage(q);
    m.attachments = attachmentsFor(m.id);
    return m;
}

bool Store::updateMessage(
    Id id, const QString& content, std::int64_t editedAt, const std::vector<Id>& mentions, const QByteArray& encrypted)
{
    return exec(
        QStringLiteral("UPDATE messages SET content = ?, edited_at = ?, mentions = ?, encrypted = ? WHERE id = ?"),
        {content, qint64(editedAt), joinIds(mentions),
            encrypted.isEmpty() ? QVariant(QMetaType(QMetaType::QByteArray)) : QVariant(encrypted), sid(id)});
}

std::vector<DeviceKeyRecord> Store::deviceKeys(const std::vector<Id>& userIds)
{
    std::vector<DeviceKeyRecord> out;
    for (Id user : userIds) {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "SELECT user_id, public_key, created_at FROM device_keys WHERE user_id = ? ORDER BY created_at"));
        q.addBindValue(sid(user));
        q.exec();
        while (q.next())
            out.push_back({uid(q.value(0)), q.value(1).toByteArray(), q.value(2).toLongLong()});
    }
    return out;
}

bool Store::addDeviceKey(const DeviceKeyRecord& k)
{
    return exec(QStringLiteral("INSERT OR IGNORE INTO device_keys(user_id, public_key, created_at) VALUES(?,?,?)"),
        {sid(k.userId), k.publicKey, qint64(k.createdAt)});
}

bool Store::removeDeviceKey(Id userId, const QByteArray& publicKey)
{
    return exec(
        QStringLiteral("DELETE FROM device_keys WHERE user_id = ? AND public_key = ?"), {sid(userId), publicKey});
}

bool Store::deleteMessage(Id id)
{
    return exec(QStringLiteral("DELETE FROM messages WHERE id = ?"), {sid(id)});
}

std::vector<MessageRecord> Store::messagePage(Id channelId, Id beforeId, int limit, bool* hasMore)
{
    std::vector<MessageRecord> out;
    QSqlQuery q(m_db);
    const QString cols = QLatin1StringView(kMessageColumns);
    if (beforeId) {
        q.prepare(QStringLiteral("SELECT %1 FROM messages WHERE channel_id = ? AND (created_at, id) < "
                                 "(SELECT created_at, id FROM messages WHERE id = ?) "
                                 "ORDER BY created_at DESC, id DESC LIMIT ?")
                .arg(cols));
        q.addBindValue(sid(channelId));
        q.addBindValue(sid(beforeId));
    } else {
        q.prepare(QStringLiteral("SELECT %1 FROM messages WHERE channel_id = ? "
                                 "ORDER BY created_at DESC, id DESC LIMIT ?")
                .arg(cols));
        q.addBindValue(sid(channelId));
    }
    q.addBindValue(limit + 1);
    q.exec();
    while (q.next())
        out.push_back(readMessage(q));
    if (hasMore)
        *hasMore = static_cast<int>(out.size()) > limit;
    if (static_cast<int>(out.size()) > limit)
        out.resize(static_cast<size_t>(limit));
    for (auto& m : out)
        m.attachments = attachmentsFor(m.id);
    return out;
}

std::vector<MessageRecord> Store::searchMessages(const std::vector<Id>& channelIds, const QString& query, int limit)
{
    // Quote every term so user input can never use FTS5 query syntax.
    QStringList terms;
    for (QString term : query.split(u' ', Qt::SkipEmptyParts)) {
        term.replace(u'"', QStringLiteral("\"\""));
        terms << u'"' + term + u'"';
    }
    std::vector<MessageRecord> out;
    if (terms.isEmpty() || channelIds.empty())
        return out;
    // Channel ids are integers, so they are bound one placeholder each.
    QStringList placeholders;
    for (size_t i = 0; i < channelIds.size(); ++i)
        placeholders << QStringLiteral("?");
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT m.id, m.channel_id, m.author_id, m.content, m.reply_to, m.edited_at, "
                             "m.is_action, m.mentions, m.encrypted, m.created_at FROM messages_fts f "
                             "JOIN messages m ON m.id = f.rowid WHERE messages_fts MATCH ? "
                             "AND m.channel_id IN (%1) ORDER BY m.created_at DESC, m.id DESC LIMIT ?")
            .arg(placeholders.join(u',')));
    q.addBindValue(terms.join(u' '));
    for (Id id : channelIds)
        q.addBindValue(sid(id));
    q.addBindValue(limit);
    q.exec();
    while (q.next())
        out.push_back(readMessage(q));
    for (auto& m : out)
        m.attachments = attachmentsFor(m.id);
    return out;
}

// ---------------------------------------------------------- attachments

bool Store::insertAttachment(const AttachmentRecord& a)
{
    return exec(QStringLiteral("INSERT INTO attachments(id, channel_id, uploader_id, message_id, filename, "
                               "mime_type, size, sha256, created_at, artwork) VALUES(?,?,?,NULL,?,?,?,?,?,?)"),
        {sid(a.id), sid(a.channelId), sid(a.uploaderId), a.filename, a.mimeType, static_cast<qint64>(a.size), a.sha256,
            qint64(a.createdAt), a.artwork ? 1 : 0});
}

std::optional<AttachmentRecord> Store::attachment(Id id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM attachments WHERE id = ?").arg(QLatin1StringView(kAttachmentColumns)));
    q.addBindValue(sid(id));
    if (!q.exec() || !q.next())
        return std::nullopt;
    return readAttachment(q);
}

std::vector<AttachmentRecord> Store::attachmentsFor(Id messageId)
{
    std::vector<AttachmentRecord> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM attachments WHERE message_id = ? ORDER BY id")
            .arg(QLatin1StringView(kAttachmentColumns)));
    q.addBindValue(sid(messageId));
    q.exec();
    while (q.next())
        out.push_back(readAttachment(q));
    return out;
}

int Store::pendingAttachmentCount(Id uploaderId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM attachments WHERE uploader_id = ? AND message_id IS NULL "
                             "AND id NOT IN (SELECT attachment_id FROM emoji) "
                             "AND id NOT IN (SELECT icon_attachment_id FROM channels) "
                             "AND id NOT IN (SELECT banner_attachment_id FROM channels) "
                             "AND id NOT IN (SELECT icon_attachment_id FROM servers) "
                             "AND id NOT IN (SELECT banner_attachment_id FROM servers)"));
    q.addBindValue(sid(uploaderId));
    if (!q.exec() || !q.next())
        return 0;
    return q.value(0).toInt();
}

std::vector<Id> Store::purgePendingAttachments(std::int64_t cutoffMs)
{
    std::vector<Id> ids;
    QSqlQuery q(m_db);
    // Attachments backing a custom emoji stay message_id NULL forever, so
    // they're excluded here rather than counted as an abandoned upload.
    q.prepare(QStringLiteral("SELECT id FROM attachments WHERE message_id IS NULL AND created_at < ? "
                             "AND id NOT IN (SELECT attachment_id FROM emoji) "
                             "AND id NOT IN (SELECT icon_attachment_id FROM channels) "
                             "AND id NOT IN (SELECT banner_attachment_id FROM channels) "
                             "AND id NOT IN (SELECT icon_attachment_id FROM servers) "
                             "AND id NOT IN (SELECT banner_attachment_id FROM servers)"));
    q.addBindValue(qint64(cutoffMs));
    q.exec();
    while (q.next())
        ids.push_back(uid(q.value(0)));
    for (Id id : ids)
        deleteAttachment(id);
    return ids;
}

bool Store::deleteAttachment(Id id)
{
    return exec(QStringLiteral("DELETE FROM attachments WHERE id = ?"), {sid(id)});
}

Id Store::artworkChannel(Id attachmentId)
{
    QSqlQuery q(m_db);
    q.prepare(
        QStringLiteral("SELECT id FROM channels WHERE icon_attachment_id = ? OR banner_attachment_id = ? LIMIT 1"));
    q.addBindValue(sid(attachmentId));
    q.addBindValue(sid(attachmentId));
    return q.exec() && q.next() ? uid(q.value(0)) : 0;
}

Id Store::artworkServer(Id attachmentId)
{
    QSqlQuery q(m_db);
    q.prepare(
        QStringLiteral("SELECT id FROM servers WHERE icon_attachment_id = ? OR banner_attachment_id = ? LIMIT 1"));
    q.addBindValue(sid(attachmentId));
    q.addBindValue(sid(attachmentId));
    return q.exec() && q.next() ? uid(q.value(0)) : 0;
}

std::vector<Id> Store::allAttachmentIds()
{
    std::vector<Id> ids;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT id FROM attachments"));
    while (q.next())
        ids.push_back(uid(q.value(0)));
    return ids;
}

bool Store::setReaction(Id messageId, Id userId, const QString& emoji, bool add)
{
    if (add)
        return exec(QStringLiteral("INSERT OR IGNORE INTO reactions(message_id, user_id, emoji) VALUES(?,?,?)"),
            {sid(messageId), sid(userId), emoji});
    return exec(QStringLiteral("DELETE FROM reactions WHERE message_id = ? AND user_id = ? AND emoji = ?"),
        {sid(messageId), sid(userId), emoji});
}

std::vector<ReactionSummary> Store::reactions(Id messageId, Id viewerId)
{
    std::vector<ReactionSummary> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT emoji, COUNT(*), MAX(user_id = ?) FROM reactions WHERE message_id = ? "
                             "GROUP BY emoji ORDER BY MIN(rowid)"));
    q.addBindValue(sid(viewerId));
    q.addBindValue(sid(messageId));
    q.exec();
    while (q.next())
        out.push_back(ReactionSummary{q.value(0).toString(), q.value(1).toUInt(), q.value(2).toBool()});
    return out;
}

bool Store::insertEmoji(const EmojiRecord& e)
{
    return exec(
        QStringLiteral(
            "INSERT INTO emoji(id, server_id, name, attachment_id, uploader_id, created_at) VALUES(?,?,?,?,?,?)"),
        {sid(e.id), sid(e.serverId), e.name, sid(e.attachmentId), sid(e.uploaderId), qint64(e.createdAt)});
}

bool Store::deleteEmoji(Id id)
{
    return exec(QStringLiteral("DELETE FROM emoji WHERE id = ?"), {sid(id)});
}

std::optional<EmojiRecord> Store::emoji(Id id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, server_id, name, attachment_id, uploader_id, created_at "
                             "FROM emoji WHERE id = ?"));
    q.addBindValue(sid(id));
    q.exec();
    if (!q.next())
        return std::nullopt;
    return EmojiRecord{uid(q.value(0)), uid(q.value(1)), q.value(2).toString(), uid(q.value(3)), uid(q.value(4)),
        q.value(5).toLongLong()};
}

std::vector<EmojiRecord> Store::emojiFor(Id serverId)
{
    std::vector<EmojiRecord> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, server_id, name, attachment_id, uploader_id, created_at "
                             "FROM emoji WHERE server_id = ? ORDER BY name"));
    q.addBindValue(sid(serverId));
    q.exec();
    while (q.next()) {
        out.push_back(EmojiRecord{uid(q.value(0)), uid(q.value(1)), q.value(2).toString(), uid(q.value(3)),
            uid(q.value(4)), q.value(5).toLongLong()});
    }
    return out;
}

int Store::emojiCount(Id serverId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM emoji WHERE server_id = ?"));
    q.addBindValue(sid(serverId));
    q.exec();
    return q.next() ? q.value(0).toInt() : 0;
}

} // namespace omachat::server
