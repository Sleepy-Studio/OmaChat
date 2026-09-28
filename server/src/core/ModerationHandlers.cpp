#include "core/ChatServer.hpp"
#include "omachat/core/Log.hpp"
#include "omachat/core/Validation.hpp"

namespace omachat::server {

using namespace omachat::permissions;

void ChatServer::publishPermissionsChanged(Id serverId)
{
    const ServerRecord* srv = m_state.server(serverId);
    if (!srv)
        return;
    std::vector<Id> members;
    for (const auto& [uid, m] : srv->members)
        members.push_back(uid);
    proto::Event e;
    e.mutable_permissions_changed()->set_server_id(serverId);
    publish(std::move(e), std::move(members));
    refreshVoicePermissions();
}

void ChatServer::removeMemberFromServer(Id serverId, Id userId, const QString& reason)
{
    const ServerRecord* srv = m_state.server(serverId);
    if (!srv)
        return;
    auto vit = m_voice.find(userId);
    if (vit != m_voice.end() && srv->channels.contains(vit->second.channelId))
        leaveVoice(userId);

    std::vector<Id> audience;
    for (const auto& [uid, m] : srv->members)
        audience.push_back(uid);
    m_store.deleteMember(serverId, userId);
    m_state.removeMember(serverId, userId);

    proto::Event e;
    auto* l = e.mutable_member_leave();
    l->set_server_id(serverId);
    l->set_user_id(userId);
    l->set_reason(reason.toStdString());
    publish(std::move(e), std::move(audience));
}

void ChatServer::handleKick(Session& s, std::uint64_t rid, const proto::KickRequest& m)
{
    const ServerRecord* srv = m_state.server(m.server_id());
    if (!srv || !srv->members.contains(s.userId) || !srv->members.contains(m.user_id())) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("member not found"));
        return;
    }
    if (!m_state.canInServer(srv->id, s.userId, KickMembers)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot kick members"));
        return;
    }
    if (m.user_id() == srv->ownerId || m.user_id() == s.userId
        || m_state.rank(srv->id, m.user_id()) >= m_state.rank(srv->id, s.userId)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("that member outranks you"));
        return;
    }
    OMA_INFO("moderation", "kick", {"server", srv->id}, {"target", m.user_id()}, {"actor", s.userId});
    removeMemberFromServer(srv->id, m.user_id(), QStringLiteral("kicked"));
    replyOk(s, rid);
}

void ChatServer::handleBan(Session& s, std::uint64_t rid, const proto::BanRequest& m)
{
    const ServerRecord* srv = m_state.server(m.server_id());
    if (!srv || !srv->members.contains(s.userId) || !m_state.user(m.user_id())) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("user not found"));
        return;
    }
    if (!m_state.canInServer(srv->id, s.userId, BanMembers)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot ban members"));
        return;
    }
    if (m.user_id() == srv->ownerId || m.user_id() == s.userId
        || m_state.rank(srv->id, m.user_id()) >= m_state.rank(srv->id, s.userId)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("that member outranks you"));
        return;
    }
    const QString reason = QString::fromStdString(m.reason()).left(512);
    m_store.insertBan(srv->id, m.user_id(), s.userId, reason, now());
    m_state.addBan(srv->id, m.user_id());
    OMA_INFO("moderation", "ban", {"server", srv->id}, {"target", m.user_id()}, {"actor", s.userId});
    if (srv->members.contains(m.user_id()))
        removeMemberFromServer(srv->id, m.user_id(), QStringLiteral("banned"));
    replyOk(s, rid);
}

void ChatServer::handleUnban(Session& s, std::uint64_t rid, const proto::UnbanRequest& m)
{
    const ServerRecord* srv = m_state.server(m.server_id());
    if (!srv || !m_state.canInServer(srv->id, s.userId, BanMembers)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot unban members"));
        return;
    }
    m_store.deleteBan(srv->id, m.user_id());
    m_state.removeBan(srv->id, m.user_id());
    replyOk(s, rid);
}

void ChatServer::handleCreateRole(Session& s, std::uint64_t rid, const proto::CreateRoleRequest& m)
{
    const ServerRecord* srv = m_state.server(m.server_id());
    if (!srv || !m_state.canInServer(m.server_id(), s.userId, ManageRoles)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot manage roles"));
        return;
    }
    const auto name = validation::roleName(QString::fromStdString(m.name()));
    if (!name) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("role names are 1-32 characters"));
        return;
    }
    if (srv->roles.size() >= 50) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("role limit reached"));
        return;
    }
    const Bits actorPerms = m_state.serverPermissions(srv->id, s.userId);
    if ((m.permissions() & ~actorPerms) != 0 || (m.permissions() & ~kAll) != 0) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot grant permissions you lack"));
        return;
    }
    RoleRecord r{m_ids.next(), srv->id, *name, m.permissions(), 1, m.color() & 0xFFFFFFu, false};
    if (!m_store.insertRole(r)) {
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not create role"));
        return;
    }
    m_state.putRole(r);
    std::vector<Id> members;
    for (const auto& [uid, mem] : srv->members)
        members.push_back(uid);
    proto::Event e;
    *e.mutable_role_update() = toProto(r);
    publish(e, members);
    proto::Envelope env;
    *env.mutable_role() = toProto(r);
    reply(s, rid, std::move(env));
}

void ChatServer::handleUpdateRole(Session& s, std::uint64_t rid, const proto::UpdateRoleRequest& m)
{
    const RoleRecord* existing = m_state.role(m.role_id());
    if (!existing || !m_state.canInServer(existing->serverId, s.userId, ManageRoles)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot manage roles"));
        return;
    }
    const Id serverId = existing->serverId;
    const auto actorRank = m_state.rank(serverId, s.userId);
    if (existing->position >= actorRank || m.position() >= actorRank) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("that role is not below your highest role"));
        return;
    }
    const Bits actorPerms = m_state.serverPermissions(serverId, s.userId);
    if ((m.permissions() & ~actorPerms) != 0 || (m.permissions() & ~kAll) != 0) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot grant permissions you lack"));
        return;
    }
    RoleRecord r = *existing;
    if (!m.name().empty() && !r.isDefault) {
        const auto name = validation::roleName(QString::fromStdString(m.name()));
        if (!name) {
            replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("role names are 1-32 characters"));
            return;
        }
        r.name = *name;
    }
    r.permissions = m.permissions();
    r.color = m.color() & 0xFFFFFFu;
    r.position = r.isDefault ? 0 : std::max<std::uint32_t>(1, m.position());
    if (!m_store.updateRole(r)) {
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not update role"));
        return;
    }
    m_state.putRole(r);
    std::vector<Id> members;
    for (const auto& [uid, mem] : m_state.server(serverId)->members)
        members.push_back(uid);
    proto::Event e;
    *e.mutable_role_update() = toProto(r);
    publish(e, members);
    publishPermissionsChanged(serverId);
    proto::Envelope env;
    *env.mutable_role() = toProto(r);
    reply(s, rid, std::move(env));
}

void ChatServer::handleDeleteRole(Session& s, std::uint64_t rid, const proto::DeleteRoleRequest& m)
{
    const RoleRecord* existing = m_state.role(m.role_id());
    if (!existing || !m_state.canInServer(existing->serverId, s.userId, ManageRoles)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot manage roles"));
        return;
    }
    if (existing->isDefault || existing->position >= m_state.rank(existing->serverId, s.userId)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("that role cannot be deleted by you"));
        return;
    }
    const Id serverId = existing->serverId;
    const Id roleId = existing->id;
    m_store.deleteRole(roleId);
    m_state.removeRole(serverId, roleId);
    std::vector<Id> members;
    for (const auto& [uid, mem] : m_state.server(serverId)->members)
        members.push_back(uid);
    proto::Event e;
    auto* d = e.mutable_role_delete();
    d->set_server_id(serverId);
    d->set_role_id(roleId);
    publish(e, members);
    publishPermissionsChanged(serverId);
    replyOk(s, rid);
}

void ChatServer::handleAssignRole(Session& s, std::uint64_t rid, const proto::AssignRoleRequest& m)
{
    const ServerRecord* srv = m_state.server(m.server_id());
    const RoleRecord* role = m_state.role(m.role_id());
    if (!srv || !role || role->serverId != srv->id || !srv->members.contains(m.user_id())) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("member or role not found"));
        return;
    }
    if (!m_state.canInServer(srv->id, s.userId, ManageRoles)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot manage roles"));
        return;
    }
    const auto actorRank = m_state.rank(srv->id, s.userId);
    if (role->isDefault || role->position >= actorRank
        || (m.user_id() != s.userId && m_state.rank(srv->id, m.user_id()) >= actorRank)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("that role or member outranks you"));
        return;
    }
    m_store.setMemberRole(srv->id, m.user_id(), role->id, m.add());
    m_state.setMemberRole(srv->id, m.user_id(), role->id, m.add());
    std::vector<Id> members;
    for (const auto& [uid, mem] : srv->members)
        members.push_back(uid);
    proto::Event e;
    *e.mutable_member_update() = toProto(*m_state.member(srv->id, m.user_id()));
    publish(e, members);
    publishPermissionsChanged(srv->id);
    replyOk(s, rid);
}

void ChatServer::handleSetOverride(Session& s, std::uint64_t rid, const proto::SetOverrideRequest& m)
{
    const auto& o = m.override();
    const ChannelRecord* c = m_state.channel(o.channel_id());
    if (!c || c->serverId == 0) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("channel not found"));
        return;
    }
    if (!m_state.canInServer(c->serverId, s.userId, ManageRoles)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot manage permissions"));
        return;
    }
    const int targetType = o.target_type() == proto::PermissionOverride::TARGET_USER ? 1 : 0;
    if (targetType == 0) {
        const RoleRecord* role = m_state.role(o.target_id());
        if (!role || role->serverId != c->serverId) {
            replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("role not found"));
            return;
        }
    } else if (!m_state.member(c->serverId, o.target_id())) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("member not found"));
        return;
    }
    const Bits actorPerms = m_state.serverPermissions(c->serverId, s.userId);
    if (((o.allow() | o.deny()) & ~actorPerms) != 0 || ((o.allow() | o.deny()) & Administrator)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot override permissions you lack"));
        return;
    }
    if (m.remove()) {
        m_store.deleteOverride(c->id, targetType, o.target_id());
        m_state.removeOverride(c->id, targetType, o.target_id());
    } else {
        OverrideRecord rec{c->id, targetType, o.target_id(), o.allow() & kAll, o.deny() & kAll};
        m_store.upsertOverride(rec);
        m_state.putOverride(rec);
    }
    publishPermissionsChanged(c->serverId);
    replyOk(s, rid);
}

void ChatServer::handleListOverrides(Session& s, std::uint64_t rid, const proto::ListOverridesRequest& m)
{
    const ChannelRecord* c = m_state.channel(m.channel_id());
    if (!c || c->serverId == 0 || !m_state.canInServer(c->serverId, s.userId, ManageRoles)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot view permissions"));
        return;
    }
    proto::Envelope env;
    auto* list = env.mutable_override_list();
    for (const auto& o : m_state.overridesFor(c->id)) {
        auto* p = list->add_overrides();
        p->set_channel_id(o.channelId);
        p->set_target_type(
            o.targetType ? proto::PermissionOverride::TARGET_USER : proto::PermissionOverride::TARGET_ROLE);
        p->set_target_id(o.targetId);
        p->set_allow(o.allow);
        p->set_deny(o.deny);
    }
    reply(s, rid, std::move(env));
}

} // namespace omachat::server
