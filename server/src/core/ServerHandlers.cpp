#include "auth/Credentials.hpp"
#include "core/ChatServer.hpp"
#include "omachat/core/Log.hpp"
#include "omachat/core/Validation.hpp"

namespace omachat::server {

using namespace omachat::permissions;

namespace {

QString normalizeTextChannelName(QString name)
{
    name = name.trimmed().toLower();
    name.replace(u' ', u'-');
    return name;
}

} // namespace

void ChatServer::handleSync(Session& s, std::uint64_t rid)
{
    proto::Envelope env;
    auto* st = env.mutable_sync_state();
    *st->mutable_self() = toProto(*m_state.user(s.userId));

    std::set<Id> users{s.userId};
    std::set<Id> visibleVoiceChannels;
    for (Id sid : m_state.serversOf(s.userId)) {
        const ServerRecord* srv = m_state.server(sid);
        auto* ps = st->add_servers();
        ps->set_id(srv->id);
        ps->set_name(srv->name.toStdString());
        ps->set_icon_url(srv->iconUrl.toStdString());
        ps->set_owner_id(srv->ownerId);
        st->add_server_permissions_server_ids(sid);
        st->add_server_permissions(m_state.serverPermissions(sid, s.userId));
        for (const auto& [rid2, role] : srv->roles)
            *st->add_roles() = toProto(role);
        for (const auto& [uid, member] : srv->members) {
            *st->add_members() = toProto(member);
            users.insert(uid);
        }
        for (Id cid : srv->channels) {
            const ChannelRecord* c = m_state.channel(cid);
            if (!c)
                continue;
            // Categories are visible if any child is; keep it simple and
            // always ship categories, filtering leaf channels by permission.
            if (c->kind != ChannelKind::Category && !m_state.can(cid, s.userId, ViewChannel))
                continue;
            *st->add_channels() = toProto(*c, s.userId);
            if (c->kind == ChannelKind::Voice)
                visibleVoiceChannels.insert(cid);
        }
    }
    for (Id cid : m_state.dmChannelsOf(s.userId)) {
        const ChannelRecord* c = m_state.channel(cid);
        *st->add_channels() = toProto(*c, s.userId);
        for (Id r : c->recipients)
            users.insert(r);
    }
    for (Id uid : users) {
        if (const UserRecord* u = m_state.user(uid))
            *st->add_users() = toProto(*u);
    }
    for (const auto& [uid, v] : m_voice) {
        if (visibleVoiceChannels.contains(v.channelId))
            *st->add_voice_states() = toProto(uid, v);
    }
    st->set_last_sequence(m_events.lastSequence());
    reply(s, rid, std::move(env));
}

void ChatServer::handleCreateServer(Session& s, std::uint64_t rid, const proto::CreateServerRequest& m)
{
    const auto name = validation::serverName(QString::fromStdString(m.name()));
    if (!name) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("server names are 1-100 characters"));
        return;
    }
    int owned = 0;
    for (const auto& [id, srv] : m_state.servers())
        owned += srv.ownerId == s.userId ? 1 : 0;
    if (owned >= 100) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("server ownership limit reached"));
        return;
    }

    const auto t = now();
    ServerRecord srv;
    srv.id = m_ids.next();
    srv.name = *name;
    srv.ownerId = s.userId;

    struct Preset {
        const char* name;
        Bits perms;
        std::uint32_t position;
        bool isDefault;
    };
    const Preset presets[] = {
        {"Guest", kGuestDefaults, 0, true},
        {"Member", kMemberDefaults, 1, false},
        {"Moderator", kModeratorDefaults, 2, false},
        {"Admin", kAdminDefaults, 3, false},
        {"Owner", kAdminDefaults, 4, false},
    };
    Id memberRole = 0;
    Id ownerRole = 0;
    for (const Preset& p : presets) {
        RoleRecord r{m_ids.next(), srv.id, QString::fromLatin1(p.name), p.perms, p.position, 0, p.isDefault};
        if (p.position == 1)
            memberRole = r.id;
        if (p.position == 4)
            ownerRole = r.id;
        srv.roles.emplace(r.id, r);
    }
    MemberRecord owner{srv.id, s.userId, t, {memberRole, ownerRole}};
    srv.members.emplace(s.userId, owner);

    const ChannelRecord textCat{
        m_ids.next(), srv.id, QStringLiteral("Text Channels"), ChannelKind::Category, 0, 0, {}, {}};
    const ChannelRecord general{
        m_ids.next(), srv.id, QStringLiteral("general"), ChannelKind::Text, textCat.id, 0, {}, {}};
    const ChannelRecord voiceCat{
        m_ids.next(), srv.id, QStringLiteral("Voice Channels"), ChannelKind::Category, 0, 1, {}, {}};
    const ChannelRecord lounge{
        m_ids.next(), srv.id, QStringLiteral("General"), ChannelKind::Voice, voiceCat.id, 0, {}, {}};

    m_store.begin();
    bool ok = m_store.insertServer(srv, t);
    for (const auto& [id, r] : srv.roles)
        ok = ok && m_store.insertRole(r);
    ok = ok && m_store.insertMember(owner);
    for (const auto* c : {&textCat, &general, &voiceCat, &lounge})
        ok = ok && m_store.insertChannel(*c, t);
    if (!ok || !m_store.commit()) {
        m_store.rollback();
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not create server"));
        return;
    }
    m_state.putServer(srv);
    for (const auto* c : {&textCat, &general, &voiceCat, &lounge})
        m_state.putChannel(*c);

    OMA_INFO("servers", "server created", {"server", srv.id}, {"owner", s.userId});

    proto::Event e;
    auto* ps = e.mutable_server_create();
    ps->set_id(srv.id);
    ps->set_name(srv.name.toStdString());
    ps->set_owner_id(srv.ownerId);
    publish(e, {s.userId});

    proto::Envelope env;
    *env.mutable_server() = *ps;
    reply(s, rid, std::move(env));
}

void ChatServer::handleLeaveServer(Session& s, std::uint64_t rid, const proto::LeaveServerRequest& m)
{
    const ServerRecord* srv = m_state.server(m.server_id());
    if (!srv || !srv->members.contains(s.userId)) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("not a member of that server"));
        return;
    }
    if (srv->ownerId == s.userId) {
        replyError(
            s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("the owner cannot leave; delete the server instead"));
        return;
    }
    removeMemberFromServer(m.server_id(), s.userId, QStringLiteral("left"));
    replyOk(s, rid);
}

void ChatServer::handleDeleteServer(Session& s, std::uint64_t rid, const proto::DeleteServerRequest& m)
{
    const ServerRecord* srv = m_state.server(m.server_id());
    if (!srv || !srv->members.contains(s.userId)) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("server not found"));
        return;
    }
    if (srv->ownerId != s.userId) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("only the owner can delete a server"));
        return;
    }
    std::vector<Id> members;
    for (const auto& [uid, mem] : srv->members)
        members.push_back(uid);
    for (Id uid : members) {
        auto vit = m_voice.find(uid);
        if (vit != m_voice.end() && srv->channels.contains(vit->second.channelId))
            leaveVoice(uid);
    }
    if (!m_store.deleteServer(m.server_id())) {
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not delete server"));
        return;
    }
    m_state.removeServer(m.server_id());
    proto::Event e;
    e.mutable_server_delete()->set_server_id(m.server_id());
    publish(std::move(e), members);
    OMA_INFO("servers", "server deleted", {"server", m.server_id()});
    replyOk(s, rid);
}

void ChatServer::handleCreateInvite(Session& s, std::uint64_t rid, const proto::CreateInviteRequest& m)
{
    if (!limit(s, rid, s.invites))
        return;
    const ServerRecord* srv = m_state.server(m.server_id());
    if (!srv || !srv->members.contains(s.userId)) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("server not found"));
        return;
    }
    if (!m_state.canInServer(m.server_id(), s.userId, CreateInvites)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot create invites here"));
        return;
    }
    const std::uint32_t expiresIn = std::min<std::uint32_t>(m.expires_in_seconds(), 30u * 86400u);
    InviteRecord inv;
    inv.token = auth::inviteToken();
    inv.serverId = m.server_id();
    inv.creatorId = s.userId;
    inv.createdAt = now();
    inv.expiresAt = expiresIn ? inv.createdAt + std::int64_t(expiresIn) * 1000 : 0;
    inv.maxUses = std::min<std::uint32_t>(m.max_uses(), 10000);
    if (!m_store.insertInvite(inv)) {
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not create invite"));
        return;
    }
    proto::Envelope env;
    auto* pi = env.mutable_invite();
    pi->set_token(inv.token.toStdString());
    pi->set_server_id(inv.serverId);
    pi->set_expires_at(inv.expiresAt);
    pi->set_max_uses(inv.maxUses);
    pi->set_server_name(srv->name.toStdString());
    reply(s, rid, std::move(env));
}

void ChatServer::handleListInvites(Session& s, std::uint64_t rid, const proto::ListInvitesRequest& m)
{
    const ServerRecord* srv = m_state.server(m.server_id());
    if (!srv || !m_state.canInServer(m.server_id(), s.userId, ManageServer)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot manage invites here"));
        return;
    }
    proto::Envelope env;
    auto* list = env.mutable_invite_list();
    for (const auto& inv : m_store.invitesForServer(m.server_id())) {
        auto* pi = list->add_invites();
        pi->set_token(inv.token.toStdString());
        pi->set_server_id(inv.serverId);
        pi->set_expires_at(inv.expiresAt);
        pi->set_max_uses(inv.maxUses);
        pi->set_uses(inv.uses);
        pi->set_server_name(srv->name.toStdString());
    }
    reply(s, rid, std::move(env));
}

void ChatServer::handleJoinInvite(Session& s, std::uint64_t rid, const proto::JoinInviteRequest& m)
{
    if (!limit(s, rid, s.invites))
        return;
    const QString token = QString::fromStdString(m.token()).trimmed();
    const auto inv = token.size() <= 64 ? m_store.inviteByToken(token) : std::nullopt;
    const bool expired = inv && inv->expiresAt && inv->expiresAt < now();
    const bool exhausted = inv && inv->maxUses && inv->uses >= inv->maxUses;
    const ServerRecord* srv = inv ? m_state.server(inv->serverId) : nullptr;
    if (!inv || expired || exhausted || !srv) {
        replyError(s, rid, proto::ERROR_INVITE_INVALID, QStringLiteral("that invite is invalid or has expired"));
        return;
    }
    if (srv->bans.contains(s.userId)) {
        replyError(s, rid, proto::ERROR_BANNED, QStringLiteral("you are banned from this server"));
        return;
    }
    proto::Envelope env;
    auto* ps = env.mutable_server();
    ps->set_id(srv->id);
    ps->set_name(srv->name.toStdString());
    ps->set_owner_id(srv->ownerId);
    if (srv->members.contains(s.userId)) {
        reply(s, rid, std::move(env));
        return;
    }
    Id memberRole = 0;
    for (const auto& [id, r] : srv->roles) {
        if (r.name == u"Member" && r.position == 1)
            memberRole = id;
    }
    MemberRecord member{srv->id, s.userId, now(), {}};
    if (memberRole)
        member.roles.insert(memberRole);

    m_store.begin();
    if (!m_store.consumeInvite(token) || !m_store.insertMember(member) || !m_store.commit()) {
        m_store.rollback();
        replyError(s, rid, proto::ERROR_INVITE_INVALID, QStringLiteral("that invite is no longer usable"));
        return;
    }
    m_state.putMember(member);
    OMA_INFO("servers", "member joined", {"server", srv->id}, {"user", s.userId});

    std::vector<Id> audience;
    for (const auto& [uid, mem] : srv->members)
        audience.push_back(uid);
    proto::Event userEvent;
    *userEvent.mutable_user_update() = toProto(*m_state.user(s.userId));
    publish(userEvent, audience);
    proto::Event joinEvent;
    *joinEvent.mutable_member_join() = toProto(member);
    publish(joinEvent, audience);

    reply(s, rid, std::move(env));
}

void ChatServer::handleCreateChannel(Session& s, std::uint64_t rid, const proto::CreateChannelRequest& m)
{
    const ServerRecord* srv = m_state.server(m.server_id());
    if (!srv || !srv->members.contains(s.userId)) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("server not found"));
        return;
    }
    if (!m_state.canInServer(m.server_id(), s.userId, CreateChannel)
        && !m_state.canInServer(m.server_id(), s.userId, ManageChannel)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot create channels here"));
        return;
    }
    const auto type = m.type();
    if (type != proto::CHANNEL_TYPE_TEXT && type != proto::CHANNEL_TYPE_VOICE && type != proto::CHANNEL_TYPE_CATEGORY) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("invalid channel type"));
        return;
    }
    QString raw = QString::fromStdString(m.name());
    if (type == proto::CHANNEL_TYPE_TEXT)
        raw = normalizeTextChannelName(raw);
    const auto name = validation::channelName(raw);
    if (!name) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("channel names are 1-64 characters"));
        return;
    }
    if (srv->channels.size() >= 500) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("channel limit reached"));
        return;
    }
    Id parent = m.parent_id();
    if (parent) {
        const ChannelRecord* p = m_state.channel(parent);
        if (!p || p->serverId != srv->id || p->kind != ChannelKind::Category || type == proto::CHANNEL_TYPE_CATEGORY) {
            replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("invalid parent category"));
            return;
        }
    }
    std::uint32_t position = 0;
    for (Id cid : srv->channels) {
        const ChannelRecord* c = m_state.channel(cid);
        if (c && c->parentId == parent)
            position = std::max(position, c->position + 1);
    }
    ChannelRecord c{m_ids.next(), srv->id, *name, static_cast<ChannelKind>(type), parent, position, {}, {}};
    if (!m_store.insertChannel(c, now())) {
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not create channel"));
        return;
    }
    m_state.putChannel(c);
    proto::Event e;
    *e.mutable_channel_create() = toProto(c, 0);
    std::vector<Id> audience;
    if (c.kind == ChannelKind::Category) {
        for (const auto& [uid, mem] : m_state.server(c.serverId)->members)
            audience.push_back(uid);
    } else {
        audience = m_state.channelAudience(c.id);
    }
    publish(e, audience);

    proto::Envelope env;
    *env.mutable_channel() = toProto(c, s.userId);
    reply(s, rid, std::move(env));
}

void ChatServer::handleUpdateChannel(Session& s, std::uint64_t rid, const proto::UpdateChannelRequest& m)
{
    ChannelRecord* c = m_state.channel(m.channel_id());
    if (!c || !m_state.can(c->id, s.userId, ViewChannel)) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("channel not found"));
        return;
    }
    if (c->serverId == 0 || !m_state.can(c->id, s.userId, ManageChannel)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot manage this channel"));
        return;
    }
    ChannelRecord updated = *c;
    if (!m.name().empty()) {
        QString raw = QString::fromStdString(m.name());
        if (c->kind == ChannelKind::Text)
            raw = normalizeTextChannelName(raw);
        const auto name = validation::channelName(raw);
        if (!name) {
            replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("channel names are 1-64 characters"));
            return;
        }
        updated.name = *name;
    }
    if (m.set_topic()) {
        const auto topic = validation::topic(QString::fromStdString(m.topic()));
        if (!topic) {
            replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("topics are at most 512 characters"));
            return;
        }
        updated.topic = *topic;
    }
    if (!m_store.updateChannel(updated)) {
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not update channel"));
        return;
    }
    *c = updated;
    proto::Event e;
    *e.mutable_channel_update() = toProto(updated, 0);
    publish(e, m_state.channelAudience(updated.id));
    proto::Envelope env;
    *env.mutable_channel() = toProto(updated, s.userId);
    reply(s, rid, std::move(env));
}

void ChatServer::handleDeleteChannel(Session& s, std::uint64_t rid, const proto::DeleteChannelRequest& m)
{
    const ChannelRecord* c = m_state.channel(m.channel_id());
    if (!c || c->serverId == 0) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("channel not found"));
        return;
    }
    if (!m_state.can(c->id, s.userId, ManageChannel) && !m_state.canInServer(c->serverId, s.userId, ManageChannel)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot delete this channel"));
        return;
    }
    const Id channelId = c->id;
    const Id serverId = c->serverId;
    std::vector<Id> inVoice;
    for (const auto& [uid, v] : m_voice) {
        if (v.channelId == channelId)
            inVoice.push_back(uid);
    }
    for (Id uid : inVoice)
        leaveVoice(uid);

    std::vector<Id> audience;
    for (const auto& [uid, mem] : m_state.server(serverId)->members)
        audience.push_back(uid);
    if (!m_store.deleteChannel(channelId)) {
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not delete channel"));
        return;
    }
    m_state.removeChannel(channelId);
    proto::Event e;
    auto* d = e.mutable_channel_delete();
    d->set_channel_id(channelId);
    d->set_server_id(serverId);
    publish(e, audience);
    replyOk(s, rid);
}

void ChatServer::handleOpenDm(Session& s, std::uint64_t rid, const proto::OpenDmRequest& m)
{
    const Id target = m.user_id();
    if (target == s.userId || !m_state.user(target)) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("user not found"));
        return;
    }
    if (auto existing = m_state.findDm(s.userId, target)) {
        proto::Envelope env;
        *env.mutable_channel() = toProto(*m_state.channel(*existing), s.userId);
        reply(s, rid, std::move(env));
        return;
    }
    // Direct messages require a shared server, so strangers cannot spam.
    if (!m_state.audienceOf(s.userId).contains(target)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you share no server with that user"));
        return;
    }
    ChannelRecord c{m_ids.next(), 0, QString(), ChannelKind::Dm, 0, 0, {},
        {std::min(s.userId, target), std::max(s.userId, target)}};
    if (!m_store.insertChannel(c, now())) {
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not open conversation"));
        return;
    }
    m_state.putChannel(c);
    proto::Event e;
    *e.mutable_channel_create() = toProto(c, 0);
    publish(e, c.recipients);
    proto::Envelope env;
    *env.mutable_channel() = toProto(c, s.userId);
    reply(s, rid, std::move(env));
}

} // namespace omachat::server
