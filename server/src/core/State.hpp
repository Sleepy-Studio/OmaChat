#pragma once

#include "omachat/core/Permissions.hpp"
#include "storage/Records.hpp"

#include <map>
#include <optional>
#include <set>
#include <vector>

namespace omachat::server {

// Authoritative in-memory mirror of guild structure (servers, roles,
// members, channels, overrides, users). Messages are never cached here.
// Every mutation is persisted by the caller before being applied.
class State {
public:
    void load(std::vector<UserRecord> users, std::vector<ServerRecord> servers, std::vector<ChannelRecord> channels,
        std::vector<OverrideRecord> overrides);

    // ---- lookup
    const UserRecord* user(Id id) const;
    const UserRecord* userByName(const QString& username) const;
    const ServerRecord* server(Id id) const;
    ServerRecord* server(Id id);
    const ChannelRecord* channel(Id id) const;
    ChannelRecord* channel(Id id);
    const RoleRecord* role(Id roleId) const;
    const MemberRecord* member(Id serverId, Id userId) const;

    const std::map<Id, ServerRecord>& servers() const { return m_servers; }
    const std::map<Id, ChannelRecord>& channels() const { return m_channels; }

    std::vector<Id> serversOf(Id userId) const;
    std::vector<Id> dmChannelsOf(Id userId) const;
    std::optional<Id> findDm(Id a, Id b) const;

    // Users who share at least one server or DM with `userId` (excluding it).
    std::set<Id> audienceOf(Id userId) const;

    // ---- mutation (in-memory only)
    void putUser(const UserRecord& u);
    void removeUser(Id id);
    void putServer(ServerRecord s);
    void removeServer(Id id);
    void putChannel(ChannelRecord c);
    void removeChannel(Id id);
    void putRole(const RoleRecord& r);
    void removeRole(Id serverId, Id roleId);
    void putMember(MemberRecord m);
    void removeMember(Id serverId, Id userId);
    void setMemberRole(Id serverId, Id userId, Id roleId, bool add);
    void putOverride(const OverrideRecord& o);
    void removeOverride(Id channelId, int targetType, Id targetId);
    void addBan(Id serverId, Id userId);
    void removeBan(Id serverId, Id userId);
    std::vector<OverrideRecord> overridesFor(Id channelId) const;

    // ---- permissions (resolved server-side, cached until the next change)
    permissions::Bits serverPermissions(Id serverId, Id userId) const;
    permissions::Bits channelPermissions(Id channelId, Id userId) const;
    bool can(Id channelId, Id userId, permissions::Bits required) const;
    bool canInServer(Id serverId, Id userId, permissions::Bits required) const;

    // Highest role position held by the member (owner = max).
    std::uint32_t rank(Id serverId, Id userId) const;

    // Users that may observe events in this channel (VIEW_CHANNEL or DM recipient).
    std::vector<Id> channelAudience(Id channelId) const;

    void invalidatePermissions() const { m_permCache.clear(); }

private:
    std::map<Id, UserRecord> m_users;
    std::map<QString, Id> m_usernames;
    std::map<Id, ServerRecord> m_servers;
    std::map<Id, ChannelRecord> m_channels;
    std::map<Id, Id> m_roleServer; // role id -> server id
    std::map<Id, std::vector<OverrideRecord>> m_overrides;

    struct CacheKey {
        Id scope; // server id or channel id
        Id user;
        bool channel;
        auto operator<=>(const CacheKey&) const = default;
    };
    mutable std::map<CacheKey, permissions::Bits> m_permCache;
};

} // namespace omachat::server
