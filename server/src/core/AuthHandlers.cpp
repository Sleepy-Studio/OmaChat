#include "auth/Credentials.hpp"
#include "auth/OAuthProviders.hpp"
#include "core/ChatServer.hpp"
#include "omachat/core/Log.hpp"
#include "omachat/core/Validation.hpp"
#include <QUrl>
#include "omachat/core/Version.hpp"

#include <QThreadPool>

namespace omachat::server {

void ChatServer::handleUpdateProfile(Session& s, std::uint64_t rid, const proto::UpdateProfileRequest& m)
{
    if (!limit(s, rid, s.presence))
        return;
    const auto name = validation::displayName(QString::fromStdString(m.display_name()));
    const QString bio = QString::fromStdString(m.bio()).trimmed();
    const QString avatar = QString::fromStdString(m.avatar_url()).trimmed();
    bool validBio = bio.size() <= 300;
    for (QChar c : bio) {
        if ((c.category() == QChar::Other_Control && c != u'\n') || c.category() == QChar::Other_Format)
            validBio = false;
    }
    const QUrl url(avatar);
    if (!name || !validBio || avatar.size() > 2048
        || (!avatar.isEmpty() && (!url.isValid() || url.scheme() != u"https" || url.host().isEmpty()
            || !url.userInfo().isEmpty()))) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("invalid profile fields"));
        return;
    }
    UserRecord updated = *m_state.user(s.userId);
    updated.displayName = *name;
    updated.avatarUrl = avatar;
    updated.bio = bio;
    if (!m_store.updateUserProfile(s.userId, *name, avatar, bio)) {
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not save profile"));
        return;
    }
    m_state.putUser(updated);
    proto::Event event;
    *event.mutable_user_update() = toProto(updated);
    const auto audience = m_state.audienceOf(s.userId);
    std::vector<Id> recipients(audience.begin(), audience.end());
    recipients.push_back(s.userId);
    publish(event, recipients);
    replyOk(s, rid);
}

namespace {

TokenBucket& bucketFor(std::map<QString, TokenBucket>& map, const QString& key, double capacity, double rate)
{
    auto it = map.find(key);
    if (it == map.end())
        it = map.emplace(key, TokenBucket(capacity, rate)).first;
    return it->second;
}

} // namespace

void ChatServer::handleHello(Session& s, std::uint64_t rid, const proto::Hello& m)
{
    if (m.protocol_major() != kProtocolMajor) {
        replyError(s, rid, proto::ERROR_PROTOCOL_MISMATCH,
            QStringLiteral("server speaks protocol %1.x, client sent %2.%3")
                .arg(kProtocolMajor)
                .arg(m.protocol_major())
                .arg(m.protocol_minor()));
        s.conn->close();
        return;
    }
    proto::Envelope env;
    auto* r = env.mutable_hello_reply();
    r->set_protocol_major(kProtocolMajor);
    r->set_protocol_minor(kProtocolMinor);
    r->set_server_version(kVersion);
    r->add_capabilities("voice.opus");
    r->add_capabilities("media.chacha20poly1305");
    r->add_capabilities("search.fts");
    r->add_capabilities("search.server");
    r->add_capabilities("dm.group");
    r->add_capabilities("resume");
    r->add_capabilities("attachments");
    r->add_capabilities("attachments.resume");
    r->add_capabilities("video.h264");
    r->add_capabilities("e2e.v1");
    r->add_capabilities("profile.v1");
    r->add_capabilities("discord.import");
    r->set_instance_name(m_config.instanceName.toStdString());
    r->set_registration_open(m_config.registrationOpen);
    r->set_media_udp_port(mediaPort());
    r->set_max_upload_bytes(maxUploadBytes());
    const auto addProvider = [&](proto::OAuthProvider provider, const OAuthProviderSettings& settings) {
        if (!settings.enabled)
            return;
        const auto* meta = auth::metaFor(provider);
        if (!meta)
            return;
        auto* p = r->add_oauth_providers();
        p->set_provider(provider);
        p->set_client_id(settings.clientId.toStdString());
        p->set_authorize_url(meta->authorizeUrl.toStdString());
        p->set_scope(meta->scope.toStdString());
    };
    addProvider(proto::OAUTH_PROVIDER_DISCORD, m_config.oauthDiscord);
    addProvider(proto::OAUTH_PROVIDER_GITHUB, m_config.oauthGithub);
    addProvider(proto::OAUTH_PROVIDER_GOOGLE, m_config.oauthGoogle);
    s.phase = Session::Phase::AwaitAuth;
    reply(s, rid, std::move(env));
}

void ChatServer::handleRegister(Session& s, std::uint64_t rid, const proto::RegisterRequest& m)
{
    if (!m_config.registrationOpen) {
        replyError(s, rid, proto::ERROR_REGISTRATION_CLOSED, QStringLiteral("registration is closed on this server"));
        return;
    }
    auto& ipBucket = bucketFor(m_registerByIp, s.peer.toString(), 5, 5.0 / 600.0);
    if (!ipBucket.tryConsume()) {
        replyError(
            s, rid, proto::ERROR_RATE_LIMITED, QStringLiteral("too many registrations"), ipBucket.retryAfterMs());
        return;
    }
    const auto username = validation::username(QString::fromStdString(m.username()));
    if (!username) {
        replyError(
            s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("usernames are 2-32 characters: a-z, 0-9, '_', '.', '-'"));
        return;
    }
    const QString password = QString::fromStdString(m.password());
    if (!validation::passwordAcceptable(password)) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("passwords need at least 8 characters"));
        return;
    }
    QString display = QString::fromStdString(m.display_name());
    if (display.trimmed().isEmpty())
        display = *username;
    const auto displayName = validation::displayName(display);
    if (!displayName) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("invalid display name"));
        return;
    }
    if (m_store.userByName(*username)) {
        replyError(s, rid, proto::ERROR_CONFLICT, QStringLiteral("that username is taken"));
        return;
    }

    s.authInFlight = true;
    const quint64 connId = s.connId;
    QThreadPool::globalInstance()->start([this, connId, rid, password, name = *username, disp = *displayName] {
        const auto hash = auth::hashPassword(password);
        QMetaObject::invokeMethod(
            this,
            [this, connId, rid, hash, name, disp] {
                Session* sp = sessionFor(connId);
                if (!sp)
                    return;
                sp->authInFlight = false;
                if (!hash) {
                    replyError(*sp, rid, proto::ERROR_INTERNAL, QStringLiteral("could not hash password"));
                    return;
                }
                UserRecord user{m_ids.next(), name, disp, QString(), *hash, now()};
                if (!m_store.insertUser(user)) {
                    replyError(*sp, rid, proto::ERROR_CONFLICT, QStringLiteral("that username is taken"));
                    return;
                }
                user.passwordHash.clear();
                m_state.putUser(user);
                OMA_INFO("auth", "user registered", {"user", user.id}, {"username", name});

                const QString refresh = auth::randomToken();
                const SessionRecord session{m_ids.next(), user.id, auth::tokenDigest(refresh),
                    now() + std::int64_t(m_config.refreshTokenDays) * 86400000};
                m_store.insertSession(session);
                completeAuth(*sp, rid, user.id, session.id, refresh);
            },
            Qt::QueuedConnection);
    });
}

void ChatServer::handleLogin(Session& s, std::uint64_t rid, const proto::LoginRequest& m)
{
    const QString name = QString::fromStdString(m.username()).trimmed().toLower();
    auto& ipBucket = bucketFor(m_authByIp, s.peer.toString(), 10, 10.0 / 60.0);
    auto& nameBucket = bucketFor(m_authByName, name, 5, 5.0 / 60.0);
    if (!ipBucket.tryConsume() || !nameBucket.tryConsume()) {
        OMA_WARN("auth", "login rate limited", {"peer", s.peer.toString()});
        replyError(s, rid, proto::ERROR_RATE_LIMITED, QStringLiteral("too many login attempts, wait a minute"),
            std::max(ipBucket.retryAfterMs(), nameBucket.retryAfterMs()));
        return;
    }
    const QString password = QString::fromStdString(m.password());
    const auto user = m_store.userByName(name);
    // Always run the verifier so response timing does not reveal whether
    // the username exists.
    const QString hash = user ? user->passwordHash : auth::dummyHash();
    const Id userId = user ? user->id : 0;

    s.authInFlight = true;
    const quint64 connId = s.connId;
    QThreadPool::globalInstance()->start([this, connId, rid, password, hash, userId] {
        const bool ok = auth::verifyPassword(password, hash) && userId != 0;
        QMetaObject::invokeMethod(
            this,
            [this, connId, rid, ok, userId] {
                Session* sp = sessionFor(connId);
                if (!sp)
                    return;
                sp->authInFlight = false;
                if (!ok) {
                    OMA_INFO("auth", "login rejected", {"peer", sp->peer.toString()});
                    replyError(*sp, rid, proto::ERROR_AUTHENTICATION, QStringLiteral("wrong username or password"));
                    return;
                }
                const QString refresh = auth::randomToken();
                const SessionRecord session{m_ids.next(), userId, auth::tokenDigest(refresh),
                    now() + std::int64_t(m_config.refreshTokenDays) * 86400000};
                m_store.insertSession(session);
                OMA_INFO("auth", "login", {"user", userId}, {"session", session.id});
                completeAuth(*sp, rid, userId, session.id, refresh);
            },
            Qt::QueuedConnection);
    });
}

void ChatServer::handleRefresh(Session& s, std::uint64_t rid, const proto::RefreshRequest& m)
{
    auto& ipBucket = bucketFor(m_authByIp, s.peer.toString(), 10, 10.0 / 60.0);
    if (!ipBucket.tryConsume()) {
        replyError(s, rid, proto::ERROR_RATE_LIMITED, QStringLiteral("too many attempts"), ipBucket.retryAfterMs());
        return;
    }
    const auto session = m_store.sessionByDigest(auth::tokenDigest(QString::fromStdString(m.refresh_token())));
    if (!session || session->expiresAt < now() || !m_state.user(session->userId)) {
        replyError(s, rid, proto::ERROR_AUTHENTICATION, QStringLiteral("session expired, log in again"));
        return;
    }
    // Rotate: the presented refresh token is single-use.
    const QString refresh = auth::randomToken();
    m_store.rotateSession(
        session->id, auth::tokenDigest(refresh), now() + std::int64_t(m_config.refreshTokenDays) * 86400000);
    completeAuth(s, rid, session->userId, session->id, refresh);
}

void ChatServer::handleResume(Session& s, std::uint64_t rid, const proto::ResumeRequest& m)
{
    const QString token = QString::fromStdString(m.access_token());
    auto it = m_accessTokens.find(token);
    if (it == m_accessTokens.end() || it->expiresAt < now() || it->sessionId != m.session_id()
        || !m_state.user(it->userId)) {
        replyError(s, rid, proto::ERROR_AUTHENTICATION, QStringLiteral("access token expired"));
        return;
    }
    const AccessGrant grant = *it;
    attachUser(s, grant.userId, grant.sessionId, token);

    std::vector<proto::Event> missed;
    const bool ok
        = m_events.replay(grant.userId, m.last_sequence(), [&](const proto::Event& e) { missed.push_back(e); });
    if (!ok) {
        // Authenticated, but the gap cannot be filled: client must SyncRequest.
        replyError(s, rid, proto::ERROR_RESUME_FAILED, QStringLiteral("full synchronization required"));
        return;
    }
    proto::Envelope env;
    env.mutable_resume_result()->set_replayed_events(missed.size());
    reply(s, rid, std::move(env));
    for (const auto& e : missed) {
        proto::Envelope ev;
        *ev.mutable_event() = personalize(e, grant.userId);
        s.conn->send(ev);
    }
    OMA_DEBUG("auth", "session resumed", {"user", grant.userId}, {"replayed", static_cast<qint64>(missed.size())});
}

void ChatServer::handleLogout(Session& s, std::uint64_t rid)
{
    m_store.deleteSession(s.sessionId);
    m_accessTokens.remove(s.accessToken);
    replyOk(s, rid);
    s.conn->close();
}

void ChatServer::handleDeleteAccount(Session& s, std::uint64_t rid)
{
    const Id userId = s.userId;
    struct RemovedServer { Id id; std::vector<Id> members; };
    struct LeftServer { Id id; std::vector<Id> members; };
    struct RemovedDm { Id id; std::vector<Id> recipients; };
    std::vector<RemovedServer> owned;
    std::vector<LeftServer> joined;
    std::vector<RemovedDm> dms;
    for (const auto& [id, srv] : m_state.servers()) {
        if (!srv.members.contains(userId))
            continue;
        std::vector<Id> members;
        for (const auto& [uid, member] : srv.members)
            members.push_back(uid);
        if (srv.ownerId == userId)
            owned.push_back({id, std::move(members)});
        else
            joined.push_back({id, std::move(members)});
    }
    for (const auto& [id, channel] : m_state.channels()) {
        if (channel.serverId == 0
            && std::ranges::find(channel.recipients, userId) != channel.recipients.end())
            dms.push_back({id, channel.recipients});
    }
    if (!m_store.deleteUser(userId)) {
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not delete account"));
        return;
    }
    leaveVoice(userId);
    for (const auto& item : owned) {
        m_state.removeServer(item.id);
        proto::Event event;
        event.mutable_server_delete()->set_server_id(item.id);
        publish(std::move(event), item.members);
    }
    for (const auto& item : joined) {
        m_state.removeMember(item.id, userId);
        proto::Event event;
        auto* leave = event.mutable_member_leave();
        leave->set_server_id(item.id);
        leave->set_user_id(userId);
        leave->set_reason("account_deleted");
        publish(std::move(event), item.members);
    }
    for (const auto& item : dms) {
        m_state.removeChannel(item.id);
        proto::Event event;
        event.mutable_channel_delete()->set_channel_id(item.id);
        publish(std::move(event), item.recipients);
    }
    m_state.removeUser(userId);
    for (auto it = m_accessTokens.begin(); it != m_accessTokens.end();) {
        it = it->userId == userId ? m_accessTokens.erase(it) : std::next(it);
    }
    if (auto it = m_offlineTimers.find(userId); it != m_offlineTimers.end()) {
        delete it->second;
        m_offlineTimers.erase(it);
    }
    m_chosenStatus.erase(userId);
    collectAttachmentGarbage();
    replyOk(s, rid);
    // Close every device after the reply has been placed on the wire.
    std::vector<quint64> connections;
    for (auto it = m_userConns.lower_bound(userId); it != m_userConns.upper_bound(userId); ++it)
        connections.push_back(it->second);
    for (quint64 connId : connections) {
        if (Session* peer = sessionFor(connId))
            peer->conn->close();
    }
}

void ChatServer::completeAuth(Session& s, std::uint64_t rid, Id userId, Id sessionId, const QString& refreshToken)
{
    const QString access = auth::randomToken();
    const std::int64_t expires = now() + std::int64_t(m_config.accessTokenMinutes) * 60000;
    m_accessTokens.insert(access, AccessGrant{userId, sessionId, expires});
    attachUser(s, userId, sessionId, access);

    proto::Envelope env;
    auto* r = env.mutable_auth_result();
    *r->mutable_user() = toProto(*m_state.user(userId));
    r->set_access_token(access.toStdString());
    r->set_refresh_token(refreshToken.toStdString());
    r->set_session_id(sessionId);
    r->set_access_expires_at(expires);
    reply(s, rid, std::move(env));
}

void ChatServer::attachUser(Session& s, Id userId, Id sessionId, const QString& accessToken)
{
    s.userId = userId;
    s.sessionId = sessionId;
    s.accessToken = accessToken;
    s.phase = Session::Phase::Ready;
    m_userConns.emplace(userId, s.connId);
    userCameOnline(userId);
}

} // namespace omachat::server
