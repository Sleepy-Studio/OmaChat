#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace omachat::server {

using Id = std::uint64_t;

struct UserRecord {
    Id id = 0;
    QString username;
    QString displayName;
    QString avatarUrl;
    QString passwordHash;
    std::int64_t createdAt = 0;
};

// A provider identity linked to a local account, keyed by (provider,
// providerUserId) so a provider-side account can only ever map to one local
// user. `provider` matches proto::OAuthProvider's name, lowercased
// (e.g. "discord").
struct OAuthIdentityRecord {
    Id id = 0;
    Id userId = 0;
    QString provider;
    QString providerUserId;
    QString providerUsername;
    std::int64_t linkedAt = 0;
};

struct SessionRecord {
    Id id = 0;
    Id userId = 0;
    QByteArray refreshDigest;
    std::int64_t expiresAt = 0;
};

struct RoleRecord {
    Id id = 0;
    Id serverId = 0;
    QString name;
    std::uint64_t permissions = 0;
    std::uint32_t position = 0;
    std::uint32_t color = 0;
    bool isDefault = false;
};

struct MemberRecord {
    Id serverId = 0;
    Id userId = 0;
    std::int64_t joinedAt = 0;
    std::set<Id> roles;
};

enum class ChannelKind : int { Text = 0, Voice = 1, Category = 2, Dm = 3, GroupDm = 4 };

struct ChannelRecord {
    Id id = 0;
    Id serverId = 0; // 0 for DMs
    QString name;
    ChannelKind kind = ChannelKind::Text;
    Id parentId = 0;
    std::uint32_t position = 0;
    QString topic;
    std::vector<Id> recipients; // DMs only
};

struct OverrideRecord {
    Id channelId = 0;
    int targetType = 0; // 0 role, 1 user
    Id targetId = 0;
    std::uint64_t allow = 0;
    std::uint64_t deny = 0;
};

struct ServerRecord {
    Id id = 0;
    QString name;
    QString iconUrl;
    Id ownerId = 0;
    std::map<Id, RoleRecord> roles;
    std::map<Id, MemberRecord> members;
    std::set<Id> channels;
    std::set<Id> bans;
    Id defaultRoleId = 0;
};

struct InviteRecord {
    QString token;
    Id serverId = 0;
    Id creatorId = 0;
    std::int64_t createdAt = 0;
    std::int64_t expiresAt = 0;
    std::uint32_t maxUses = 0;
    std::uint32_t uses = 0;
};

struct AttachmentRecord {
    Id id = 0;
    Id channelId = 0;
    Id uploaderId = 0;
    Id messageId = 0; // 0 while pending (uploaded, not yet sent)
    QString filename;
    QString mimeType;
    std::uint64_t size = 0;
    QByteArray sha256;
    std::int64_t createdAt = 0;
};

struct MessageRecord {
    Id id = 0;
    Id channelId = 0;
    Id authorId = 0;
    QString content;
    Id replyTo = 0;
    std::int64_t editedAt = 0;
    bool isAction = false;
    std::vector<Id> mentions;
    std::vector<AttachmentRecord> attachments; // declared last: aggregate init keeps working
    QByteArray encrypted; // end-to-end payload (DMs); content is empty then
};

struct DeviceKeyRecord {
    Id userId = 0;
    QByteArray publicKey; // X25519, 32 bytes
    std::int64_t createdAt = 0;
};

struct ReactionSummary {
    QString emoji;
    std::uint32_t count = 0;
    bool me = false;
};

} // namespace omachat::server
