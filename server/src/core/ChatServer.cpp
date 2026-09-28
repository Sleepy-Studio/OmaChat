#include "core/ChatServer.hpp"

#include "omachat/core/Log.hpp"
#include "omachat/core/Version.hpp"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSslConfiguration>

namespace omachat::server {

using proto::ErrorCode;

ChatServer::ChatServer(ServerConfig config, QObject* parent)
    : QObject(parent)
    , m_config(std::move(config))
    , m_ids(m_config.nodeId)
    , m_relay(this)
{
    connect(&m_listener, &QSslServer::pendingConnectionAvailable, this, &ChatServer::onPendingConnection);
    connect(&m_listener, &QSslServer::errorOccurred, this, [](QSslSocket* socket, QAbstractSocket::SocketError) {
        OMA_DEBUG("transport", "tls accept error", {"peer", socket->peerAddress().toString()},
            {"error", socket->errorString()});
    });
    connect(&m_listener, &QSslServer::handshakeInterruptedOnError, this, [](QSslSocket* socket, const QSslError& e) {
        OMA_DEBUG("transport", "tls handshake failed", {"peer", socket->peerAddress().toString()},
            {"error", e.errorString()});
    });

    // Low-frequency housekeeping only: expire tokens/sessions. No polling of
    // client state happens here.
    m_housekeeping.setInterval(std::chrono::minutes(5));
    connect(&m_housekeeping, &QTimer::timeout, this, [this] {
        const auto t = now();
        for (auto it = m_accessTokens.begin(); it != m_accessTokens.end();) {
            it = it->expiresAt < t ? m_accessTokens.erase(it) : std::next(it);
        }
        m_store.purgeExpiredSessions(t);
        collectAttachmentGarbage();
        // Buckets refill fully in under a minute; dropping idle ones is safe.
        m_authByIp.clear();
        m_authByName.clear();
    });
}

ChatServer::~ChatServer()
{
    for (auto& [id, timer] : m_offlineTimers)
        delete timer;
}

std::int64_t ChatServer::now() const
{
    return QDateTime::currentMSecsSinceEpoch();
}

bool ChatServer::start(const TlsIdentity& identity, QString* error)
{
    const QFileInfo dbInfo(m_config.databasePath);
    QDir().mkpath(dbInfo.absolutePath());
    if (!m_store.open(m_config.databasePath, error))
        return false;

    // Only tighten permissions on a directory we create; an operator-made
    // files directory keeps whatever mode they chose.
    if (!QFileInfo::exists(m_config.filesPath)) {
        if (!QDir().mkpath(m_config.filesPath)) {
            if (error)
                *error = QStringLiteral("cannot create files directory %1").arg(m_config.filesPath);
            return false;
        }
        QFile::setPermissions(m_config.filesPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                | QFileDevice::ExeOwner);
    }
    collectAttachmentGarbage();

    auto snap = m_store.loadSnapshot();
    m_state.load(m_store.allUsers(), std::move(snap.servers), std::move(snap.channels), std::move(snap.overrides));

    QSslConfiguration tls = QSslConfiguration::defaultConfiguration();
    tls.setLocalCertificateChain(identity.chain);
    tls.setPrivateKey(identity.key);
    tls.setProtocol(QSsl::TlsV1_3OrLater);
    tls.setPeerVerifyMode(QSslSocket::VerifyNone); // clients authenticate with credentials, not certificates
    m_listener.setSslConfiguration(tls);
    m_listener.setHandshakeTimeout(10000);

    if (!m_listener.listen(QHostAddress(m_config.bind), m_config.port)) {
        if (error)
            *error = QStringLiteral("cannot listen on %1:%2: %3")
                         .arg(m_config.bind)
                         .arg(m_config.port)
                         .arg(m_listener.errorString());
        return false;
    }
    if (!m_relay.bind(QHostAddress(m_config.mediaBind), m_config.mediaPort, error)) {
        if (error)
            *error = QStringLiteral("cannot bind media UDP %1:%2: %3")
                         .arg(m_config.mediaBind)
                         .arg(m_config.mediaPort)
                         .arg(*error);
        return false;
    }
    m_housekeeping.start();
    OMA_INFO("server", "listening", {"control", QStringLiteral("%1:%2").arg(m_config.bind).arg(controlPort())},
        {"media", QStringLiteral("%1:%2").arg(m_config.mediaBind).arg(mediaPort())},
        {"servers", static_cast<qint64>(m_state.servers().size())}, {"registration", m_config.registrationOpen});
    return true;
}

quint16 ChatServer::controlPort() const
{
    return m_listener.serverPort();
}

// ------------------------------------------------------------ connections

void ChatServer::onPendingConnection()
{
    while (m_listener.hasPendingConnections()) {
        auto* socket = qobject_cast<QSslSocket*>(m_listener.nextPendingConnection());
        if (!socket)
            continue;
        const QString ip = socket->peerAddress().toString();
        if (m_connectionsPerIp.value(ip) >= m_config.maxConnectionsPerIp) {
            OMA_WARN("transport", "connection limit per address reached", {"peer", ip});
            socket->abort();
            socket->deleteLater();
            continue;
        }
        m_connectionsPerIp[ip] += 1;

        auto session = std::make_unique<Session>();
        session->connId = m_nextConnId++;
        session->peer = socket->peerAddress();
        session->conn = new protocol::ProtocolConnection(socket, this);
        const quint64 id = session->connId;
        connect(session->conn, &protocol::ProtocolConnection::envelopeReceived, this,
            [this, id](const proto::Envelope& env) { onEnvelope(id, env); });
        connect(session->conn, &protocol::ProtocolConnection::closed, this, [this, id] { onClosed(id); });
        connect(session->conn, &protocol::ProtocolConnection::protocolError, this,
            [ip](const QString& reason) { OMA_WARN("transport", "protocol error", {"peer", ip}, {"reason", reason}); });
        // Unauthenticated connections must finish the handshake promptly.
        QTimer::singleShot(std::chrono::seconds(30), this, [this, id] {
            Session* s = sessionFor(id);
            if (s && s->phase != Session::Phase::Ready && !s->authInFlight)
                s->conn->abort();
        });
        m_sessions.emplace(id, std::move(session));
    }
}

ChatServer::Session* ChatServer::sessionFor(quint64 connId)
{
    auto it = m_sessions.find(connId);
    return it == m_sessions.end() ? nullptr : it->second.get();
}

void ChatServer::onClosed(quint64 connId)
{
    auto it = m_sessions.find(connId);
    if (it == m_sessions.end())
        return;
    std::unique_ptr<Session> s = std::move(it->second);
    m_sessions.erase(it);

    abortUploadsOf(connId);
    const QString ip = s->peer.toString();
    if (--m_connectionsPerIp[ip] <= 0)
        m_connectionsPerIp.remove(ip);

    if (s->userId) {
        for (auto uit = m_userConns.lower_bound(s->userId); uit != m_userConns.upper_bound(s->userId);) {
            uit = uit->second == connId ? m_userConns.erase(uit) : std::next(uit);
        }
        if (!m_userConns.contains(s->userId))
            scheduleOffline(s->userId);
    }
    s->conn->deleteLater();
}

void ChatServer::onEnvelope(quint64 connId, const proto::Envelope& env)
{
    Session* sp = sessionFor(connId);
    if (!sp)
        return;
    Session& s = *sp;
    const auto rid = env.request_id();
    using P = proto::Envelope::PayloadCase;

    if (!s.general.tryConsume()) {
        replyError(s, rid, proto::ERROR_RATE_LIMITED, QStringLiteral("too many requests"), s.general.retryAfterMs());
        return;
    }

    if (s.phase == Session::Phase::AwaitHello) {
        if (env.payload_case() != P::kHello) {
            replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("expected hello"));
            s.conn->close();
            return;
        }
        handleHello(s, rid, env.hello());
        return;
    }

    if (s.phase == Session::Phase::AwaitAuth) {
        if (s.authInFlight) {
            replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("authentication in progress"));
            return;
        }
        switch (env.payload_case()) {
        case P::kRegister:
            handleRegister(s, rid, env.register_());
            return;
        case P::kLogin:
            handleLogin(s, rid, env.login());
            return;
        case P::kRefresh:
            handleRefresh(s, rid, env.refresh());
            return;
        case P::kResume:
            handleResume(s, rid, env.resume());
            return;
        default:
            replyError(s, rid, proto::ERROR_NOT_AUTHENTICATED, QStringLiteral("authenticate first"));
            return;
        }
    }

    switch (env.payload_case()) {
    case P::kSync:
        handleSync(s, rid);
        break;
    case P::kLogout:
        handleLogout(s, rid);
        break;
    case P::kCreateServer:
        handleCreateServer(s, rid, env.create_server());
        break;
    case P::kLeaveServer:
        handleLeaveServer(s, rid, env.leave_server());
        break;
    case P::kDeleteServer:
        handleDeleteServer(s, rid, env.delete_server());
        break;
    case P::kCreateInvite:
        handleCreateInvite(s, rid, env.create_invite());
        break;
    case P::kListInvites:
        handleListInvites(s, rid, env.list_invites());
        break;
    case P::kJoinInvite:
        handleJoinInvite(s, rid, env.join_invite());
        break;
    case P::kCreateChannel:
        handleCreateChannel(s, rid, env.create_channel());
        break;
    case P::kUpdateChannel:
        handleUpdateChannel(s, rid, env.update_channel());
        break;
    case P::kDeleteChannel:
        handleDeleteChannel(s, rid, env.delete_channel());
        break;
    case P::kOpenDm:
        handleOpenDm(s, rid, env.open_dm());
        break;
    case P::kSendMessage:
        handleSendMessage(s, rid, env.send_message());
        break;
    case P::kEditMessage:
        handleEditMessage(s, rid, env.edit_message());
        break;
    case P::kDeleteMessage:
        handleDeleteMessage(s, rid, env.delete_message());
        break;
    case P::kGetMessages:
        handleGetMessages(s, rid, env.get_messages());
        break;
    case P::kSearchMessages:
        handleSearch(s, rid, env.search_messages());
        break;
    case P::kReaction:
        handleReaction(s, rid, env.reaction());
        break;
    case P::kTyping:
        handleTyping(s, rid, env.typing());
        break;
    case P::kSetPresence:
        handleSetPresence(s, rid, env.set_presence());
        break;
    case P::kBeginUpload:
        handleBeginUpload(s, rid, env.begin_upload());
        break;
    case P::kUploadChunk:
        handleUploadChunk(s, rid, env.upload_chunk());
        break;
    case P::kFinishUpload:
        handleFinishUpload(s, rid, env.finish_upload());
        break;
    case P::kCancelUpload:
        handleCancelUpload(s, rid, env.cancel_upload());
        break;
    case P::kDownload:
        handleDownload(s, rid, env.download());
        break;
    case P::kJoinVoice:
        handleJoinVoice(s, rid, env.join_voice());
        break;
    case P::kLeaveVoice:
        handleLeaveVoice(s, rid);
        break;
    case P::kSetVoiceState:
        handleSetVoiceState(s, rid, env.set_voice_state());
        break;
    case P::kServerMute:
        handleServerMute(s, rid, env.server_mute());
        break;
    case P::kKick:
        handleKick(s, rid, env.kick());
        break;
    case P::kBan:
        handleBan(s, rid, env.ban());
        break;
    case P::kUnban:
        handleUnban(s, rid, env.unban());
        break;
    case P::kCreateRole:
        handleCreateRole(s, rid, env.create_role());
        break;
    case P::kUpdateRole:
        handleUpdateRole(s, rid, env.update_role());
        break;
    case P::kDeleteRole:
        handleDeleteRole(s, rid, env.delete_role());
        break;
    case P::kAssignRole:
        handleAssignRole(s, rid, env.assign_role());
        break;
    case P::kSetOverride:
        handleSetOverride(s, rid, env.set_override());
        break;
    case P::kListOverrides:
        handleListOverrides(s, rid, env.list_overrides());
        break;
    default:
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("unsupported request"));
        break;
    }
}

// ------------------------------------------------------------------ replies

void ChatServer::reply(Session& s, std::uint64_t requestId, proto::Envelope&& env)
{
    env.set_request_id(requestId);
    s.conn->send(env);
}

void ChatServer::replyOk(Session& s, std::uint64_t requestId)
{
    proto::Envelope env;
    env.mutable_ok();
    reply(s, requestId, std::move(env));
}

void ChatServer::replyError(
    Session& s, std::uint64_t requestId, proto::ErrorCode code, const QString& message, int retryAfterMs)
{
    proto::Envelope env;
    env.set_request_id(requestId);
    auto* e = env.mutable_error();
    e->set_code(code);
    e->set_message(message.toStdString());
    e->set_retry_after_ms(static_cast<std::uint32_t>(std::max(0, retryAfterMs)));
    s.conn->send(env);
}

bool ChatServer::limit(Session& s, std::uint64_t requestId, TokenBucket& bucket)
{
    if (bucket.tryConsume())
        return true;
    replyError(s, requestId, proto::ERROR_RATE_LIMITED, QStringLiteral("slow down"), bucket.retryAfterMs());
    return false;
}

// ------------------------------------------------------------------- events

void ChatServer::publish(proto::Event event, std::vector<Id> recipients)
{
    m_events.append(event, recipients);
    for (Id uid : recipients)
        deliver(uid, event);
}

void ChatServer::publishEphemeral(const proto::Event& event, const std::vector<Id>& recipients, Id except)
{
    for (Id uid : recipients) {
        if (uid != except)
            deliver(uid, event);
    }
}

void ChatServer::deliver(Id userId, const proto::Event& event)
{
    auto [begin, end] = m_userConns.equal_range(userId);
    if (begin == end)
        return;
    proto::Envelope env;
    *env.mutable_event() = personalize(event, userId);
    for (auto it = begin; it != end; ++it) {
        if (Session* s = sessionFor(it->second); s && s->phase == Session::Phase::Ready)
            s->conn->send(env);
    }
}

proto::Event ChatServer::personalize(const proto::Event& event, Id userId) const
{
    proto::Event out = event;
    if (out.has_channel_create())
        out.mutable_channel_create()->set_effective_permissions(
            m_state.channelPermissions(out.channel_create().id(), userId));
    if (out.has_channel_update())
        out.mutable_channel_update()->set_effective_permissions(
            m_state.channelPermissions(out.channel_update().id(), userId));
    if (out.has_message_create() || out.has_message_update()) {
        auto* msg = out.has_message_create() ? out.mutable_message_create() : out.mutable_message_update();
        for (auto& r : *msg->mutable_reactions())
            r.set_me(false); // "me" flags are only exact in history fetches
    }
    return out;
}

// -------------------------------------------------------------- conversions

proto::UserStatus ChatServer::statusOf(Id userId) const
{
    if (!m_userConns.contains(userId) && !m_offlineTimers.contains(userId))
        return proto::USER_STATUS_OFFLINE;
    auto it = m_chosenStatus.find(userId);
    return it == m_chosenStatus.end() ? proto::USER_STATUS_ONLINE : it->second;
}

proto::User ChatServer::toProto(const UserRecord& u) const
{
    proto::User p;
    p.set_id(u.id);
    p.set_username(u.username.toStdString());
    p.set_display_name(u.displayName.toStdString());
    p.set_avatar_url(u.avatarUrl.toStdString());
    p.set_status(statusOf(u.id));
    return p;
}

proto::Channel ChatServer::toProto(const ChannelRecord& c, Id viewer) const
{
    proto::Channel p;
    p.set_id(c.id);
    p.set_server_id(c.serverId);
    p.set_name(c.name.toStdString());
    p.set_type(static_cast<proto::ChannelType>(c.kind));
    p.set_parent_id(c.parentId);
    p.set_position(c.position);
    p.set_topic(c.topic.toStdString());
    for (Id r : c.recipients)
        p.add_recipient_ids(r);
    if (viewer)
        p.set_effective_permissions(m_state.channelPermissions(c.id, viewer));
    return p;
}

proto::Role ChatServer::toProto(const RoleRecord& r) const
{
    proto::Role p;
    p.set_id(r.id);
    p.set_server_id(r.serverId);
    p.set_name(r.name.toStdString());
    p.set_permissions(r.permissions);
    p.set_position(r.position);
    p.set_color(r.color);
    p.set_is_default(r.isDefault);
    return p;
}

proto::Member ChatServer::toProto(const MemberRecord& m) const
{
    proto::Member p;
    p.set_server_id(m.serverId);
    p.set_user_id(m.userId);
    p.set_joined_at(m.joinedAt);
    for (Id r : m.roles)
        p.add_role_ids(r);
    return p;
}

proto::ChatMessage ChatServer::toProto(const MessageRecord& m, Id viewer)
{
    proto::ChatMessage p;
    p.set_id(m.id);
    p.set_channel_id(m.channelId);
    p.set_author_id(m.authorId);
    p.set_timestamp(decodeSnowflake(m.id).unixMs);
    p.set_content(m.content.toStdString());
    p.set_reply_to(m.replyTo);
    p.set_edited_at(m.editedAt);
    p.set_is_action(m.isAction);
    for (Id mention : m.mentions)
        p.add_mention_ids(mention);
    for (const auto& a : m.attachments) {
        auto* pa = p.add_attachments();
        pa->set_id(a.id);
        pa->set_filename(a.filename.toStdString());
        pa->set_mime_type(a.mimeType.toStdString());
        pa->set_size(a.size);
        pa->set_sha256(a.sha256.toStdString());
    }
    if (viewer) {
        for (const auto& r : m_store.reactions(m.id, viewer)) {
            auto* pr = p.add_reactions();
            pr->set_emoji(r.emoji.toStdString());
            pr->set_count(r.count);
            pr->set_me(r.me);
        }
    }
    return p;
}

proto::VoiceState ChatServer::toProto(Id userId, const VoiceRec& v) const
{
    proto::VoiceState p;
    p.set_user_id(userId);
    p.set_channel_id(v.channelId);
    p.set_self_mute(v.selfMute);
    p.set_self_deaf(v.selfDeaf);
    p.set_server_mute(v.serverMute);
    p.set_server_deaf(v.serverDeaf);
    p.set_stream_id(v.streamId);
    return p;
}

// ------------------------------------------------------------------ presence

void ChatServer::userCameOnline(Id userId)
{
    if (auto it = m_offlineTimers.find(userId); it != m_offlineTimers.end()) {
        // Reconnected inside the grace window: nobody saw them leave.
        delete it->second;
        m_offlineTimers.erase(it);
        return;
    }
    const bool alreadyOnline = m_userConns.count(userId) > 1;
    if (alreadyOnline)
        return;
    proto::Event e;
    auto* p = e.mutable_presence_update();
    p->set_user_id(userId);
    p->set_status(statusOf(userId));
    const auto audience = m_state.audienceOf(userId);
    publish(std::move(e), std::vector<Id>(audience.begin(), audience.end()));
}

void ChatServer::scheduleOffline(Id userId)
{
    if (m_offlineTimers.contains(userId))
        return;
    auto* timer = new QTimer();
    timer->setSingleShot(true);
    timer->setInterval(std::chrono::seconds(15));
    connect(timer, &QTimer::timeout, this, [this, userId] {
        auto it = m_offlineTimers.find(userId);
        if (it == m_offlineTimers.end())
            return;
        it->second->deleteLater();
        m_offlineTimers.erase(it);
        if (m_userConns.contains(userId))
            return;
        leaveVoice(userId);
        proto::Event e;
        auto* p = e.mutable_presence_update();
        p->set_user_id(userId);
        p->set_status(proto::USER_STATUS_OFFLINE);
        const auto audience = m_state.audienceOf(userId);
        publish(std::move(e), std::vector<Id>(audience.begin(), audience.end()));
    });
    m_offlineTimers.emplace(userId, timer);
    timer->start();
}

} // namespace omachat::server
