#include "auth/Credentials.hpp"
#include "core/ChatServer.hpp"
#include "omachat/core/Log.hpp"
#include "omachat/core/Validation.hpp"
#include "omachat/core/Version.hpp"

#include <QCoreApplication>
#include <QImageReader>

#include <algorithm>

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
        *ps = toServerProto(*srv);
        st->add_server_permissions_server_ids(sid);
        st->add_server_permissions(m_state.serverPermissions(sid, s.userId));
        for (const auto& [rid2, role] : srv->roles)
            *st->add_roles() = toProto(role);
        for (const auto& e : m_store.emojiFor(sid))
            *st->add_emoji() = toProto(e);
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

void ChatServer::handleInstanceStatus(Session& s, std::uint64_t rid)
{
    if (!isOperator(s)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("instance operator access required"));
        return;
    }
    proto::Envelope env;
    auto* status = env.mutable_instance_status();
    status->set_instance_name(m_config.instanceName.toStdString());
    status->set_server_version(kVersion);
    status->set_started_at(m_startedAt);
    status->set_registration_open(m_config.registrationOpen);
    status->set_connected_sessions(static_cast<std::uint32_t>(m_userConns.size()));
    std::uint32_t online = 0;
    Id previous = 0;
    for (const auto& [uid, connId] : m_userConns) {
        (void)connId;
        if (uid != previous) {
            ++online;
            previous = uid;
        }
    }
    status->set_online_users(online);
    status->set_total_users(static_cast<std::uint32_t>(m_state.userCount()));
    status->set_total_messages(m_store.messageCount());
    status->set_restart_available(m_config.remoteRestart);
    for (const auto& user : m_store.allUsers()) {
        auto* item = status->add_users();
        item->set_id(user.id);
        item->set_username(user.username.toStdString());
        item->set_display_name(user.displayName.toStdString());
        item->set_online(m_userConns.contains(user.id));
        item->set_suspended(m_store.isSuspended(user.id));
    }
    for (const auto& entry : m_store.recentAudit()) {
        auto* item = status->add_audit();
        item->set_at(entry.at);
        item->set_actor_id(entry.actorId);
        item->set_action(entry.action.toStdString());
        item->set_target_id(entry.targetId);
    }
    for (const auto& line : log::recentLines())
        status->add_log_lines(line.toStdString());
    for (const auto& [id, community] : m_state.servers()) {
        auto* item = status->add_communities();
        item->set_id(id);
        item->set_name(community.name.toStdString());
        item->set_owner_id(community.ownerId);
        if (const auto* owner = m_state.user(community.ownerId))
            item->set_owner_name(owner->username.toStdString());
        item->set_members(static_cast<std::uint32_t>(community.members.size()));
        item->set_channels(static_cast<std::uint32_t>(community.channels.size()));
        for (const auto& [uid, member] : community.members) {
            (void)member;
            item->add_member_ids(uid);
        }
        for (Id uid : community.bans)
            item->add_banned_user_ids(uid);
    }
    reply(s, rid, std::move(env));
}

void ChatServer::handleSetInstanceRegistration(Session& s, std::uint64_t rid, bool open)
{
    if (!isOperator(s)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("instance operator access required"));
        return;
    }
    if (!m_store.begin() || !m_store.setRegistrationOverride(open)
        || !m_store.recordAudit(
            now(), s.userId, open ? QStringLiteral("registration.open") : QStringLiteral("registration.close"), 0)
        || !m_store.commit()) {
        m_store.rollback();
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not save registration setting"));
        return;
    }
    m_config.registrationOpen = open;
    OMA_INFO("operator", "registration changed", {"operator", s.userId}, {"open", open});
    replyOk(s, rid);
}

void ChatServer::handleDeleteInstanceCommunity(Session& s, std::uint64_t rid, Id serverId)
{
    if (!isOperator(s)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("instance operator access required"));
        return;
    }
    const ServerRecord* community = m_state.server(serverId);
    if (!community) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("community not found"));
        return;
    }
    std::vector<Id> members;
    for (const auto& [uid, member] : community->members) {
        (void)member;
        members.push_back(uid);
    }
    for (Id uid : members) {
        const auto voice = m_voice.find(uid);
        if (voice != m_voice.end() && community->channels.contains(voice->second.channelId))
            leaveVoice(uid);
    }
    if (!m_store.begin() || !m_store.deleteServer(serverId)
        || !m_store.recordAudit(now(), s.userId, QStringLiteral("community.delete"), serverId) || !m_store.commit()) {
        m_store.rollback();
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not delete community"));
        return;
    }
    m_state.removeServer(serverId);
    proto::Event event;
    event.mutable_server_delete()->set_server_id(serverId);
    publish(std::move(event), members);
    OMA_WARN("operator", "community deleted", {"operator", s.userId}, {"server", serverId});
    replyOk(s, rid);
}

void ChatServer::handleSetInstanceSuspension(Session& s, std::uint64_t rid, Id userId, bool suspended)
{
    if (!isOperator(s)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("instance operator access required"));
        return;
    }
    if (!m_state.user(userId)) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("user not found"));
        return;
    }
    if (userId == m_operatorId) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("operator cannot suspend own account"));
        return;
    }
    if (!m_store.begin() || !m_store.setSuspended(userId, suspended)
        || (suspended && !m_store.deleteSessionsForUser(userId))
        || !m_store.recordAudit(
            now(), s.userId, suspended ? QStringLiteral("account.suspend") : QStringLiteral("account.restore"), userId)
        || !m_store.commit()) {
        m_store.rollback();
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not update account suspension"));
        return;
    }
    if (suspended) {
        for (auto it = m_accessTokens.begin(); it != m_accessTokens.end();)
            it = it->userId == userId ? m_accessTokens.erase(it) : std::next(it);
        std::vector<quint64> connections;
        for (auto it = m_userConns.lower_bound(userId); it != m_userConns.upper_bound(userId); ++it)
            connections.push_back(it->second);
        for (quint64 connId : connections)
            if (Session* peer = sessionFor(connId))
                peer->conn->close();
    }
    OMA_WARN(
        "operator", suspended ? "account suspended" : "account restored", {"operator", s.userId}, {"user", userId});
    replyOk(s, rid);
}

void ChatServer::handleRestartInstance(Session& s, std::uint64_t rid)
{
    if (!isOperator(s) || !m_config.remoteRestart) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("remote restart is not available"));
        return;
    }
    if (!m_store.recordAudit(now(), s.userId, QStringLiteral("instance.restart"), 0)) {
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not record restart"));
        return;
    }
    OMA_WARN("operator", "instance restart requested", {"operator", s.userId});
    replyOk(s, rid);
    // Both the packaged systemd unit and Docker Compose restart nonzero exits.
    QTimer::singleShot(250, this, [] { QCoreApplication::exit(75); });
}

void ChatServer::handleInstanceModeration(Session& s, std::uint64_t rid, const proto::InstanceModerationRequest& m)
{
    if (!isOperator(s)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("instance operator access required"));
        return;
    }
    const ServerRecord* community = m_state.server(m.server_id());
    if (!community || !m_state.user(m.user_id())) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("community or user not found"));
        return;
    }
    if (m.user_id() == community->ownerId || m.user_id() == m_operatorId) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("cannot moderate an owner account"));
        return;
    }
    QString action;
    switch (m.action()) {
    case proto::InstanceModerationRequest::KICK:
        if (!community->members.contains(m.user_id())) {
            replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("member not found"));
            return;
        }
        removeMemberFromServer(m.server_id(), m.user_id(), QStringLiteral("operator_kick"));
        action = QStringLiteral("community.kick");
        break;
    case proto::InstanceModerationRequest::BAN:
        if (!m_store.insertBan(m.server_id(), m.user_id(), s.userId, QStringLiteral("instance operator"), now())) {
            replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not ban user"));
            return;
        }
        m_state.addBan(m.server_id(), m.user_id());
        if (community->members.contains(m.user_id()))
            removeMemberFromServer(m.server_id(), m.user_id(), QStringLiteral("operator_ban"));
        action = QStringLiteral("community.ban");
        break;
    case proto::InstanceModerationRequest::UNBAN:
        if (!community->bans.contains(m.user_id())) {
            replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("ban not found"));
            return;
        }
        if (!m_store.deleteBan(m.server_id(), m.user_id())) {
            replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not unban user"));
            return;
        }
        m_state.removeBan(m.server_id(), m.user_id());
        action = QStringLiteral("community.unban");
        break;
    default:
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("invalid moderation action"));
        return;
    }
    m_store.recordAudit(now(), s.userId, action, m.user_id());
    OMA_WARN("operator", "community moderation", {"operator", s.userId}, {"server", m.server_id()},
        {"target", m.user_id()}, {"action", action});
    replyOk(s, rid);
}

void ChatServer::handleCreateServer(Session& s, std::uint64_t rid, const proto::CreateServerRequest& m)
{
    const auto name = validation::serverName(QString::fromStdString(m.name()));
    if (!name) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("server names are 1-100 characters"));
        return;
    }
    const auto description = validation::serverDescription(QString::fromStdString(m.description()));
    if (!description) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("server descriptions are at most 2000 characters"));
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
    srv.description = *description;

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
    *ps = toServerProto(srv);
    publish(e, {s.userId});

    proto::Envelope env;
    *env.mutable_server() = *ps;
    reply(s, rid, std::move(env));
}

void ChatServer::handleUpdateServer(Session& s, std::uint64_t rid, const proto::UpdateServerRequest& m)
{
    ServerRecord* srv = m_state.server(m.server_id());
    if (!srv || !srv->members.contains(s.userId)) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("server not found"));
        return;
    }
    if (!m_state.canInServer(m.server_id(), s.userId, ManageServer)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot manage this server"));
        return;
    }
    ServerRecord updated = *srv;
    if (!m.name().empty()) {
        const auto name = validation::serverName(QString::fromStdString(m.name()));
        if (!name) {
            replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("server names are 1-100 characters"));
            return;
        }
        updated.name = *name;
    }
    if (m.set_description()) {
        const auto description = validation::serverDescription(QString::fromStdString(m.description()));
        if (!description) {
            replyError(s, rid, proto::ERROR_BAD_REQUEST,
                QStringLiteral("server descriptions are at most 2000 characters"));
            return;
        }
        updated.description = *description;
    }
    // Server artwork reuses the channel upload pipeline: the attachment is
    // uploaded with channel_artwork=true against any channel of this server,
    // then referenced here. This keeps image normalization in one place.
    const auto applyArtwork = [&](Id assetId, Id& destination) {
        if (assetId == destination)
            return true;
        if (!assetId) {
            destination = 0;
            return true;
        }
        const auto asset = m_store.attachment(assetId);
        if (!asset || !asset->artwork || asset->uploaderId != s.userId || asset->messageId
            || asset->size > 2 * 1024 * 1024 || m_store.artworkChannel(assetId)
            || m_store.artworkServer(assetId))
            return false;
        const ChannelRecord* carrier = m_state.channel(asset->channelId);
        if (!carrier || carrier->serverId != srv->id)
            return false;
        QImageReader reader(attachmentPath(assetId));
        reader.setDecideFormatFromContent(true);
        const QByteArray format = reader.format().toLower();
        const QSize size = reader.size();
        if ((format != "png" && format != "jpeg" && format != "webp") || !size.isValid()
            || size.width() > 6000 || size.height() > 6000 || qint64(size.width()) * size.height() > 12000000
            || reader.imageCount() > 1 || reader.read().isNull())
            return false;
        destination = assetId;
        return true;
    };
    if (m.set_icon() && !applyArtwork(m.icon_attachment_id(), updated.iconAttachmentId)) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("invalid server icon image"));
        return;
    }
    if (m.set_banner() && !applyArtwork(m.banner_attachment_id(), updated.bannerAttachmentId)) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("invalid server banner image"));
        return;
    }
    if (!m_store.updateServer(updated)) {
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not update server"));
        return;
    }
    m_state.putServer(updated);

    proto::Event e;
    *e.mutable_server_update() = toServerProto(updated);
    std::vector<Id> audience;
    for (const auto& [uid, mem] : updated.members)
        audience.push_back(uid);
    publish(std::move(e), audience);

    proto::Envelope env;
    *env.mutable_server() = toServerProto(updated);
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
    const auto topic = validation::topic(QString::fromStdString(m.topic()));
    const auto description = validation::channelDescription(QString::fromStdString(m.description()));
    if (!topic || !description) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("invalid channel topic or description"));
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
    ChannelRecord c{m_ids.next(), srv->id, *name, static_cast<ChannelKind>(type), parent, position,
        *topic, {}, *description};
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
    // Group DMs belong to their participants: any of them may rename one.
    const bool groupDm = c->kind == ChannelKind::GroupDm;
    if (groupDm ? m.set_topic() : (c->serverId == 0 || !m_state.can(c->id, s.userId, ManageChannel))) {
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
    if (m.set_description()) {
        const auto description = validation::channelDescription(QString::fromStdString(m.description()));
        if (!description) {
            replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("descriptions are at most 2000 characters"));
            return;
        }
        updated.description = *description;
    }
    const auto applyArtwork = [&](Id assetId, Id& destination) {
        if (assetId == destination)
            return true;
        if (!assetId) {
            destination = 0;
            return true;
        }
        const auto asset = m_store.attachment(assetId);
        if (!asset || !asset->artwork || asset->channelId != c->id || asset->uploaderId != s.userId || asset->messageId
            || asset->size > 2 * 1024 * 1024 || m_store.artworkChannel(assetId)
            || m_store.artworkServer(assetId))
            return false;
        QImageReader reader(attachmentPath(assetId));
        reader.setDecideFormatFromContent(true);
        const QByteArray format = reader.format().toLower();
        const QSize size = reader.size();
        if ((format != "png" && format != "jpeg" && format != "webp") || !size.isValid()
            || size.width() > 6000 || size.height() > 6000 || qint64(size.width()) * size.height() > 12000000
            || reader.imageCount() > 1 || reader.read().isNull())
            return false;
        destination = assetId;
        return true;
    };
    if (m.set_icon() && !applyArtwork(m.icon_attachment_id(), updated.iconAttachmentId)) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("invalid channel icon image"));
        return;
    }
    if (m.set_banner() && !applyArtwork(m.banner_attachment_id(), updated.bannerAttachmentId)) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("invalid channel banner image"));
        return;
    }
    if (m.set_parent() || m.set_position()) {
        if (m.set_parent()) {
            const Id parent = m.parent_id();
            const ChannelRecord* category = parent ? m_state.channel(parent) : nullptr;
            if ((parent && (!category || category->serverId != c->serverId
                                   || category->kind != ChannelKind::Category))
                || (c->kind == ChannelKind::Category && parent)) {
                replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("invalid destination category"));
                return;
            }
            if (parent && !m_state.can(parent, s.userId, ManageChannel)
                && !m_state.canInServer(c->serverId, s.userId, ManageChannel)) {
                replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot move channels there"));
                return;
            }
            updated.parentId = parent;
        }
        if (m.set_position() && m.position() > 500) {
            replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("invalid channel position"));
            return;
        }
        auto siblings = [&](Id parent, bool categories) {
            std::vector<ChannelRecord> result;
            for (Id id : m_state.server(c->serverId)->channels) {
                const ChannelRecord* item = m_state.channel(id);
                if (!item || item->id == c->id || item->parentId != parent
                    || (parent == 0 && (item->kind == ChannelKind::Category) != categories))
                    continue;
                result.push_back(*item);
            }
            std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
                return a.position != b.position ? a.position < b.position : a.id < b.id;
            });
            return result;
        };
        const bool categories = c->kind == ChannelKind::Category;
        std::vector<ChannelRecord> affected;
        if (updated.parentId != c->parentId) {
            auto prior = siblings(c->parentId, categories);
            for (size_t i = 0; i < prior.size(); ++i) {
                prior[i].position = static_cast<std::uint32_t>(i);
                affected.push_back(prior[i]);
            }
        }
        auto destination = siblings(updated.parentId, categories);
        const size_t insertAt = m.set_position() ? std::min<size_t>(m.position(), destination.size())
                                                 : destination.size();
        destination.insert(destination.begin() + static_cast<std::ptrdiff_t>(insertAt), updated);
        for (size_t i = 0; i < destination.size(); ++i) {
            destination[i].position = static_cast<std::uint32_t>(i);
            affected.push_back(destination[i]);
        }
        if (!m_store.begin()) {
            replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not start channel move"));
            return;
        }
        for (const auto& item : affected) {
            if (!m_store.updateChannel(item)) {
                m_store.rollback();
                replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not move channel"));
                return;
            }
        }
        if (!m_store.commit()) {
            m_store.rollback();
            replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not commit channel move"));
            return;
        }
        for (const auto& item : affected) {
            m_state.putChannel(item);
            if (item.id == c->id)
                updated = item;
        }
        publishPermissionsChanged(updated.serverId);
        proto::Envelope env;
        *env.mutable_channel() = toProto(updated, s.userId);
        reply(s, rid, std::move(env));
        return;
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

namespace {
constexpr size_t kMaxGroupDmRecipients = 10;
}

void ChatServer::handleCreateGroupDm(Session& s, std::uint64_t rid, const proto::CreateGroupDmRequest& m)
{
    if (!limit(s, rid, s.conversations))
        return;
    std::vector<Id> recipients{s.userId};
    const std::set<Id> audience = m_state.audienceOf(s.userId);
    for (Id uid : m.user_ids()) {
        if (std::ranges::find(recipients, uid) != recipients.end())
            continue;
        if (!m_state.user(uid) || !audience.contains(uid)) {
            replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you share no server with a user"));
            return;
        }
        recipients.push_back(uid);
    }
    if (recipients.size() < 3 || recipients.size() > kMaxGroupDmRecipients) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST,
            QStringLiteral("a group conversation has 3 to %1 people").arg(kMaxGroupDmRecipients));
        return;
    }
    QString name;
    if (!m.name().empty()) {
        const auto valid = validation::channelName(QString::fromStdString(m.name()));
        if (!valid) {
            replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("names are 1-64 characters"));
            return;
        }
        name = *valid;
    }
    std::ranges::sort(recipients);
    ChannelRecord c{m_ids.next(), 0, name, ChannelKind::GroupDm, 0, 0, {}, recipients};
    if (!m_store.insertChannel(c, now())) {
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not create conversation"));
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

void ChatServer::handleAddGroupDmRecipient(Session& s, std::uint64_t rid, const proto::AddGroupDmRecipientRequest& m)
{
    if (!limit(s, rid, s.conversations))
        return;
    ChannelRecord* c = m_state.channel(m.channel_id());
    if (!c || c->kind != ChannelKind::GroupDm || std::ranges::find(c->recipients, s.userId) == c->recipients.end()) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("conversation not found"));
        return;
    }
    const Id uid = m.user_id();
    if (!m_state.user(uid) || !m_state.audienceOf(s.userId).contains(uid)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you share no server with that user"));
        return;
    }
    if (std::ranges::find(c->recipients, uid) != c->recipients.end()) {
        proto::Envelope env;
        *env.mutable_channel() = toProto(*c, s.userId);
        reply(s, rid, std::move(env));
        return;
    }
    if (c->recipients.size() >= kMaxGroupDmRecipients) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST,
            QStringLiteral("a group conversation has at most %1 people").arg(kMaxGroupDmRecipients));
        return;
    }
    if (!m_store.setRecipient(c->id, uid, true)) {
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not add to conversation"));
        return;
    }
    const std::vector<Id> existing = c->recipients;
    c->recipients.push_back(uid);
    std::ranges::sort(c->recipients);
    m_state.invalidatePermissions();
    proto::Event update;
    *update.mutable_channel_update() = toProto(*c, 0);
    publish(update, existing);
    proto::Event created;
    *created.mutable_channel_create() = toProto(*c, 0);
    publish(created, {uid});
    proto::Envelope env;
    *env.mutable_channel() = toProto(*c, s.userId);
    reply(s, rid, std::move(env));
}

void ChatServer::handleLeaveGroupDm(Session& s, std::uint64_t rid, const proto::LeaveGroupDmRequest& m)
{
    ChannelRecord* c = m_state.channel(m.channel_id());
    if (!c || c->kind != ChannelKind::GroupDm || std::ranges::find(c->recipients, s.userId) == c->recipients.end()) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("conversation not found"));
        return;
    }
    const Id channelId = c->id;
    const bool empty = c->recipients.size() == 1;
    const bool stored = empty ? m_store.deleteChannel(channelId) : m_store.setRecipient(channelId, s.userId, false);
    if (!stored) {
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not leave conversation"));
        return;
    }
    std::erase(c->recipients, s.userId);
    m_state.invalidatePermissions();
    if (empty) {
        m_state.removeChannel(channelId);
    } else {
        proto::Event update;
        *update.mutable_channel_update() = toProto(*c, 0);
        publish(update, c->recipients);
    }
    proto::Event gone;
    gone.mutable_channel_delete()->set_channel_id(channelId);
    publish(gone, {s.userId});
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
