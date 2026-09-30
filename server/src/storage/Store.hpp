#pragma once

#include "storage/Records.hpp"

#include <QSqlDatabase>
#include <QString>
#include <QVariant>

#include <optional>
#include <vector>

namespace omachat::server {

// Persistent storage for omachat-server. All SQL lives here, written against
// the Qt SQL abstraction in portable SQL wherever practical so that a
// PostgreSQL driver can be introduced later without touching callers.
// SQLite-specific pieces (FTS5, PRAGMAs) are confined to migrate()/search().
//
// Not thread-safe: use from the server's event-loop thread only.
class Store {
public:
    Store();
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    bool open(const QString& path, QString* error);
    int schemaVersion() const;
    std::optional<bool> registrationOverride();
    bool setRegistrationOverride(bool open);
    std::uint64_t messageCount();
    bool isSuspended(Id userId);
    bool setSuspended(Id userId, bool suspended);
    bool deleteSessionsForUser(Id userId);
    struct AuditEntry {
        std::int64_t at;
        Id actorId;
        QString action;
        Id targetId;
    };
    bool recordAudit(std::int64_t at, Id actorId, const QString& action, Id targetId);
    std::vector<AuditEntry> recentAudit();

    // Groups multi-statement mutations so a failure cannot leave half a
    // server behind.
    bool begin() { return m_db.transaction(); }
    bool commit() { return m_db.commit(); }
    void rollback() { m_db.rollback(); }

    // ---- users
    bool insertUser(const UserRecord& user); // false on username conflict
    // Removes the user, their owned servers, and conversations they participated in atomically.
    bool deleteUser(Id userId);
    std::optional<UserRecord> userByName(const QString& username);
    std::vector<UserRecord> allUsers();
    bool updateUserPasswordHash(Id userId, const QString& hash);
    bool updateUserProfile(Id userId, const QString& displayName, const QString& avatarUrl, const QString& bio);

    // ---- OAuth identities
    bool insertOAuthIdentity(const OAuthIdentityRecord& identity);
    std::optional<OAuthIdentityRecord> oauthIdentity(const QString& provider, const QString& providerUserId);
    bool updateOAuthIdentityUsername(Id identityId, const QString& providerUsername);
    std::vector<OAuthIdentityRecord> oauthIdentitiesForUser(Id userId);
    bool deleteOAuthIdentity(Id userId, const QString& provider);
    bool hasPassword(Id userId);

    // ---- sessions
    bool insertSession(const SessionRecord& s);
    std::optional<SessionRecord> sessionByDigest(const QByteArray& digest);
    bool rotateSession(Id sessionId, const QByteArray& newDigest, std::int64_t expiresAt);
    bool deleteSession(Id sessionId);
    int purgeExpiredSessions(std::int64_t nowMs);

    // ---- servers, roles, members, channels (loaded into memory at startup)
    struct Snapshot {
        std::vector<ServerRecord> servers; // with roles, members, bans, channel ids
        std::vector<ChannelRecord> channels;
        std::vector<OverrideRecord> overrides;
    };
    Snapshot loadSnapshot();

    bool insertServer(const ServerRecord& s, std::int64_t createdAt);
    bool updateServer(const ServerRecord& s);
    bool deleteServer(Id id);
    bool insertRole(const RoleRecord& r);
    bool updateRole(const RoleRecord& r);
    bool deleteRole(Id id);
    bool insertMember(const MemberRecord& m);
    bool deleteMember(Id serverId, Id userId);
    bool setMemberRole(Id serverId, Id userId, Id roleId, bool add);
    bool insertChannel(const ChannelRecord& c, std::int64_t createdAt);
    bool updateChannel(const ChannelRecord& c);
    bool setRecipient(Id channelId, Id userId, bool present); // group DMs
    bool deleteChannel(Id id);
    bool upsertOverride(const OverrideRecord& o);
    bool deleteOverride(Id channelId, int targetType, Id targetId);
    bool insertBan(Id serverId, Id userId, Id bannedBy, const QString& reason, std::int64_t at);
    bool deleteBan(Id serverId, Id userId);

    // ---- invites
    bool insertInvite(const InviteRecord& i);
    std::optional<InviteRecord> inviteByToken(const QString& token);
    bool consumeInvite(const QString& token);
    std::vector<InviteRecord> invitesForServer(Id serverId);

    // ---- messages
    // Also claims m.attachments (by id) for the message, atomically.
    bool insertMessage(const MessageRecord& m, Id importServerId = 0, const QString& discordId = {});
    std::optional<Id> discordImportId(Id serverId, const QString& kind, const QString& discordId);
    bool rememberDiscordImport(Id serverId, const QString& kind, const QString& discordId, Id localId);
    bool rememberDiscordReply(Id serverId, Id messageId, const QString& replyDiscordId);
    bool resolveDiscordReplies(Id serverId);
    std::optional<MessageRecord> message(Id id);
    bool updateMessage(Id id, const QString& content, std::int64_t editedAt, const std::vector<Id>& mentions,
        const QByteArray& encrypted = {});

    // ---- end-to-end device keys (public halves only)
    std::vector<DeviceKeyRecord> deviceKeys(const std::vector<Id>& userIds);
    bool addDeviceKey(const DeviceKeyRecord& key);
    bool removeDeviceKey(Id userId, const QByteArray& publicKey);
    bool deleteMessage(Id id);
    // Newest first, strictly older than `beforeId` (0 = newest). Fetches
    // limit + 1 rows internally to report has_more.
    std::vector<MessageRecord> messagePage(Id channelId, Id beforeId, int limit, bool* hasMore);
    // Newest matches first, across the given channels.
    std::vector<MessageRecord> searchMessages(const std::vector<Id>& channelIds, const QString& query, int limit);

    // ---- attachments (metadata only; bytes live under files.path)
    bool insertAttachment(const AttachmentRecord& a);
    std::optional<AttachmentRecord> attachment(Id id);
    std::vector<AttachmentRecord> attachmentsFor(Id messageId);
    int pendingAttachmentCount(Id uploaderId);
    // Deletes pending attachments created before `cutoffMs`; returns their ids.
    std::vector<Id> purgePendingAttachments(std::int64_t cutoffMs);
    bool deleteAttachment(Id id);
    // Returns the channel that currently uses an attachment as artwork.
    Id artworkChannel(Id attachmentId);
    // Returns the server that currently uses an attachment as icon/banner.
    Id artworkServer(Id attachmentId);
    std::vector<Id> allAttachmentIds();

    bool setReaction(Id messageId, Id userId, const QString& emoji, bool add);
    std::vector<ReactionSummary> reactions(Id messageId, Id viewerId);

    bool insertEmoji(const EmojiRecord& e);
    bool deleteEmoji(Id id);
    std::optional<EmojiRecord> emoji(Id id);
    std::vector<EmojiRecord> emojiFor(Id serverId);
    int emojiCount(Id serverId);

private:
    bool exec(const QString& sql, const std::vector<QVariant>& binds = {});
    bool migrate(QString* error);

    QString m_connectionName;
    QSqlDatabase m_db;
};

} // namespace omachat::server
