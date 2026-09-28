#include "core/State.hpp"

#include <algorithm>

namespace omachat::server {

using namespace omachat::permissions;

void State::load(std::vector<UserRecord> users, std::vector<ServerRecord> servers, std::vector<ChannelRecord> channels,
    std::vector<OverrideRecord> overrides)
{
    for (auto& u : users) {
        m_usernames[u.username] = u.id;
        m_users.emplace(u.id, std::move(u));
    }
    for (auto& s : servers)
        putServer(std::move(s));
    for (auto& c : channels)
        m_channels.emplace(c.id, std::move(c));
    for (auto& o : overrides)
        m_overrides[o.channelId].push_back(o);
    invalidatePermissions();
}

const UserRecord* State::user(Id id) const
{
    auto it = m_users.find(id);
    return it == m_users.end() ? nullptr : &it->second;
}

const ServerRecord* State::server(Id id) const
{
    auto it = m_servers.find(id);
    return it == m_servers.end() ? nullptr : &it->second;
}

ServerRecord* State::server(Id id)
{
    auto it = m_servers.find(id);
    return it == m_servers.end() ? nullptr : &it->second;
}

const ChannelRecord* State::channel(Id id) const
{
    auto it = m_channels.find(id);
    return it == m_channels.end() ? nullptr : &it->second;
}

ChannelRecord* State::channel(Id id)
{
    auto it = m_channels.find(id);
    return it == m_channels.end() ? nullptr : &it->second;
}

const RoleRecord* State::role(Id roleId) const
{
    auto sit = m_roleServer.find(roleId);
    if (sit == m_roleServer.end())
        return nullptr;
    const ServerRecord* s = server(sit->second);
    if (!s)
        return nullptr;
    auto rit = s->roles.find(roleId);
    return rit == s->roles.end() ? nullptr : &rit->second;
}

const MemberRecord* State::member(Id serverId, Id userId) const
{
    const ServerRecord* s = server(serverId);
    if (!s)
        return nullptr;
    auto it = s->members.find(userId);
    return it == s->members.end() ? nullptr : &it->second;
}

std::vector<Id> State::serversOf(Id userId) const
{
    std::vector<Id> out;
    for (const auto& [id, s] : m_servers) {
        if (s.members.contains(userId))
            out.push_back(id);
    }
    return out;
}

std::vector<Id> State::dmChannelsOf(Id userId) const
{
    std::vector<Id> out;
    for (const auto& [id, c] : m_channels) {
        if (c.serverId == 0 && std::ranges::find(c.recipients, userId) != c.recipients.end())
            out.push_back(id);
    }
    return out;
}

std::optional<Id> State::findDm(Id a, Id b) const
{
    for (const auto& [id, c] : m_channels) {
        if (c.kind != ChannelKind::Dm || c.recipients.size() != 2)
            continue;
        const bool hasA = std::ranges::find(c.recipients, a) != c.recipients.end();
        const bool hasB = std::ranges::find(c.recipients, b) != c.recipients.end();
        if (hasA && hasB)
            return id;
    }
    return std::nullopt;
}

std::set<Id> State::audienceOf(Id userId) const
{
    std::set<Id> out;
    for (const auto& [id, s] : m_servers) {
        if (!s.members.contains(userId))
            continue;
        for (const auto& [uid, m] : s.members)
            out.insert(uid);
    }
    for (Id dm : dmChannelsOf(userId)) {
        for (Id r : m_channels.at(dm).recipients)
            out.insert(r);
    }
    out.erase(userId);
    return out;
}

void State::putUser(const UserRecord& u)
{
    m_users[u.id] = u;
    m_usernames[u.username] = u.id;
}

const UserRecord* State::userByName(const QString& username) const
{
    auto it = m_usernames.find(username);
    return it == m_usernames.end() ? nullptr : user(it->second);
}

void State::putServer(ServerRecord s)
{
    for (const auto& [rid, r] : s.roles) {
        m_roleServer[rid] = s.id;
        if (r.isDefault)
            s.defaultRoleId = rid;
    }
    const Id id = s.id;
    m_servers[id] = std::move(s);
    invalidatePermissions();
}

void State::removeServer(Id id)
{
    auto it = m_servers.find(id);
    if (it == m_servers.end())
        return;
    for (Id cid : it->second.channels) {
        m_channels.erase(cid);
        m_overrides.erase(cid);
    }
    for (const auto& [rid, r] : it->second.roles)
        m_roleServer.erase(rid);
    m_servers.erase(it);
    invalidatePermissions();
}

void State::putChannel(ChannelRecord c)
{
    if (c.serverId) {
        if (ServerRecord* s = server(c.serverId))
            s->channels.insert(c.id);
    }
    const Id id = c.id;
    m_channels[id] = std::move(c);
    invalidatePermissions();
}

void State::removeChannel(Id id)
{
    auto it = m_channels.find(id);
    if (it == m_channels.end())
        return;
    if (ServerRecord* s = server(it->second.serverId)) {
        s->channels.erase(id);
        // Children of a deleted category become top-level.
        for (Id cid : s->channels) {
            auto& child = m_channels[cid];
            if (child.parentId == id)
                child.parentId = 0;
        }
    }
    m_channels.erase(it);
    m_overrides.erase(id);
    invalidatePermissions();
}

void State::putRole(const RoleRecord& r)
{
    ServerRecord* s = server(r.serverId);
    if (!s)
        return;
    s->roles[r.id] = r;
    if (r.isDefault)
        s->defaultRoleId = r.id;
    m_roleServer[r.id] = r.serverId;
    invalidatePermissions();
}

void State::removeRole(Id serverId, Id roleId)
{
    ServerRecord* s = server(serverId);
    if (!s)
        return;
    s->roles.erase(roleId);
    for (auto& [uid, m] : s->members)
        m.roles.erase(roleId);
    for (Id cid : s->channels) {
        auto oit = m_overrides.find(cid);
        if (oit == m_overrides.end())
            continue;
        std::erase_if(oit->second, [&](const OverrideRecord& o) { return o.targetType == 0 && o.targetId == roleId; });
    }
    m_roleServer.erase(roleId);
    invalidatePermissions();
}

void State::putMember(MemberRecord m)
{
    ServerRecord* s = server(m.serverId);
    if (!s)
        return;
    const Id uid = m.userId;
    s->members[uid] = std::move(m);
    invalidatePermissions();
}

void State::removeMember(Id serverId, Id userId)
{
    if (ServerRecord* s = server(serverId)) {
        s->members.erase(userId);
        invalidatePermissions();
    }
}

void State::setMemberRole(Id serverId, Id userId, Id roleId, bool add)
{
    ServerRecord* s = server(serverId);
    if (!s)
        return;
    auto it = s->members.find(userId);
    if (it == s->members.end())
        return;
    if (add)
        it->second.roles.insert(roleId);
    else
        it->second.roles.erase(roleId);
    invalidatePermissions();
}

void State::putOverride(const OverrideRecord& o)
{
    auto& list = m_overrides[o.channelId];
    auto it = std::ranges::find_if(
        list, [&](const OverrideRecord& x) { return x.targetType == o.targetType && x.targetId == o.targetId; });
    if (it == list.end())
        list.push_back(o);
    else
        *it = o;
    invalidatePermissions();
}

void State::removeOverride(Id channelId, int targetType, Id targetId)
{
    auto oit = m_overrides.find(channelId);
    if (oit == m_overrides.end())
        return;
    std::erase_if(
        oit->second, [&](const OverrideRecord& o) { return o.targetType == targetType && o.targetId == targetId; });
    invalidatePermissions();
}

void State::addBan(Id serverId, Id userId)
{
    if (ServerRecord* s = server(serverId))
        s->bans.insert(userId);
}

void State::removeBan(Id serverId, Id userId)
{
    if (ServerRecord* s = server(serverId))
        s->bans.erase(userId);
}

std::vector<OverrideRecord> State::overridesFor(Id channelId) const
{
    auto it = m_overrides.find(channelId);
    return it == m_overrides.end() ? std::vector<OverrideRecord>{} : it->second;
}

Bits State::serverPermissions(Id serverId, Id userId) const
{
    const CacheKey key{serverId, userId, false};
    if (auto it = m_permCache.find(key); it != m_permCache.end())
        return it->second;

    Bits result = 0;
    const ServerRecord* s = server(serverId);
    const MemberRecord* m = member(serverId, userId);
    if (s && m) {
        std::vector<Bits> roles;
        for (Id rid : m->roles) {
            auto rit = s->roles.find(rid);
            if (rit != s->roles.end())
                roles.push_back(rit->second.permissions);
        }
        Bits defaults = 0;
        if (auto dit = s->roles.find(s->defaultRoleId); dit != s->roles.end())
            defaults = dit->second.permissions;
        ResolveInput in;
        in.isOwner = s->ownerId == userId;
        in.defaultRole = defaults;
        in.memberRoles = roles;
        result = resolve(in);
    }
    m_permCache.emplace(key, result);
    return result;
}

Bits State::channelPermissions(Id channelId, Id userId) const
{
    const CacheKey key{channelId, userId, true};
    if (auto it = m_permCache.find(key); it != m_permCache.end())
        return it->second;

    Bits result = 0;
    const ChannelRecord* c = channel(channelId);
    if (c && c->serverId == 0) {
        // DMs: participants may read, write and react; nobody else sees them.
        if (std::ranges::find(c->recipients, userId) != c->recipients.end())
            result = ViewChannel | SendMessages | ReadHistory | AddReactions | AttachFiles;
    } else if (c) {
        const ServerRecord* s = server(c->serverId);
        const MemberRecord* m = member(c->serverId, userId);
        if (s && m) {
            std::vector<Bits> roles;
            for (Id rid : m->roles) {
                auto rit = s->roles.find(rid);
                if (rit != s->roles.end())
                    roles.push_back(rit->second.permissions);
            }
            Bits defaults = 0;
            if (auto dit = s->roles.find(s->defaultRoleId); dit != s->roles.end())
                defaults = dit->second.permissions;

            auto buildLayer = [&](Id cid) {
                ResolveInput::Layer layer;
                for (const OverrideRecord& o : overridesFor(cid)) {
                    const Override ov{o.allow, o.deny};
                    if (o.targetType == 1) {
                        if (o.targetId == userId)
                            layer.user = ov;
                    } else if (o.targetId == s->defaultRoleId) {
                        layer.defaultRole = ov;
                    } else if (m->roles.contains(o.targetId)) {
                        layer.memberRoles.push_back(ov);
                    }
                }
                return layer;
            };
            std::vector<ResolveInput::Layer> layers;
            if (c->parentId)
                layers.push_back(buildLayer(c->parentId));
            layers.push_back(buildLayer(c->id));

            ResolveInput in;
            in.isOwner = s->ownerId == userId;
            in.defaultRole = defaults;
            in.memberRoles = roles;
            in.layers = layers;
            in.channelScope = true;
            result = resolve(in);
        }
    }
    m_permCache.emplace(key, result);
    return result;
}

bool State::can(Id channelId, Id userId, Bits required) const
{
    return has(channelPermissions(channelId, userId), required);
}

bool State::canInServer(Id serverId, Id userId, Bits required) const
{
    return has(serverPermissions(serverId, userId), required);
}

std::uint32_t State::rank(Id serverId, Id userId) const
{
    const ServerRecord* s = server(serverId);
    if (!s)
        return 0;
    if (s->ownerId == userId)
        return UINT32_MAX;
    const MemberRecord* m = member(serverId, userId);
    if (!m)
        return 0;
    std::uint32_t best = 0;
    for (Id rid : m->roles) {
        auto it = s->roles.find(rid);
        if (it != s->roles.end())
            best = std::max(best, it->second.position);
    }
    return best;
}

std::vector<Id> State::channelAudience(Id channelId) const
{
    std::vector<Id> out;
    const ChannelRecord* c = channel(channelId);
    if (!c)
        return out;
    if (c->serverId == 0)
        return c->recipients;
    const ServerRecord* s = server(c->serverId);
    if (!s)
        return out;
    for (const auto& [uid, m] : s->members) {
        if (can(channelId, uid, ViewChannel))
            out.push_back(uid);
    }
    return out;
}

} // namespace omachat::server
