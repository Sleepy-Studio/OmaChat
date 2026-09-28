#include "networking/ServerConnection.hpp"

#include "omachat/core/Log.hpp"
#include "omachat/core/Version.hpp"
#include "omachat/ipc/IpcMessage.hpp"

#include <QDateTime>
#include <QNetworkInformation>
#include <QSslConfiguration>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

namespace omachat::daemon {

QString ipcErrorCode(proto::ErrorCode code)
{
    namespace e = ipc::errors;
    switch (code) {
    case proto::ERROR_BAD_REQUEST:
        return e::BadRequest;
    case proto::ERROR_PROTOCOL_MISMATCH:
        return e::ProtocolMismatch;
    case proto::ERROR_AUTHENTICATION:
    case proto::ERROR_NOT_AUTHENTICATED:
    case proto::ERROR_REGISTRATION_CLOSED:
        return e::AuthenticationError;
    case proto::ERROR_PERMISSION_DENIED:
    case proto::ERROR_BANNED:
        return e::PermissionDenied;
    case proto::ERROR_NOT_FOUND:
    case proto::ERROR_INVITE_INVALID:
        return e::NotFound;
    case proto::ERROR_CONFLICT:
        return e::Conflict;
    case proto::ERROR_RATE_LIMITED:
        return e::RateLimited;
    case proto::ERROR_TOO_LARGE:
        return e::TooLarge;
    default:
        return e::Internal;
    }
}

namespace {

QString fingerprintOf(const QSslCertificate& cert)
{
    const QByteArray digest = cert.digest(QCryptographicHash::Sha256).toHex().toUpper();
    QStringList parts;
    for (qsizetype i = 0; i < digest.size(); i += 2)
        parts << QString::fromLatin1(digest.mid(i, 2));
    return QStringLiteral("SHA256:") + parts.join(u':');
}

// Detect dead peers within ~60 s without any application-level polling.
void enableTcpKeepalive(qintptr fd)
{
    if (fd < 0)
        return;
    const int on = 1, idle = 30, interval = 10, count = 3;
    ::setsockopt(static_cast<int>(fd), SOL_SOCKET, SO_KEEPALIVE, &on, sizeof on);
    ::setsockopt(static_cast<int>(fd), IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof idle);
    ::setsockopt(static_cast<int>(fd), IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof interval);
    ::setsockopt(static_cast<int>(fd), IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof count);
    ::setsockopt(static_cast<int>(fd), IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
}

} // namespace

ServerConnection::ServerConnection(ICredentialStore& credentials, QObject* parent)
    : QObject(parent)
    , m_credentials(credentials)
{
    m_connectTimeout.setSingleShot(true);
    m_connectTimeout.setInterval(std::chrono::seconds(10));
    connect(&m_connectTimeout, &QTimer::timeout, this, [this] {
        OMA_WARN("net", "connection attempt timed out", {"host", m_account.host});
        m_errorMessage = QStringLiteral("connection timed out");
        teardownSocket();
        onSocketClosed();
    });
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &ServerConnection::connectNow);

    // Pause reconnect attempts while the system reports no network and
    // resume immediately when it returns. Purely event driven.
    if (QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability)) {
        auto* info = QNetworkInformation::instance();
        connect(info, &QNetworkInformation::reachabilityChanged, this, [this](QNetworkInformation::Reachability r) {
            const bool online = r == QNetworkInformation::Reachability::Online
                || r == QNetworkInformation::Reachability::Site || r == QNetworkInformation::Reachability::Local;
            if (!m_wantConnected)
                return;
            if (online && (m_state == State::Offline || m_state == State::Reconnecting)) {
                m_backoff.reset();
                m_reconnectTimer.stop();
                connectNow();
            } else if (!online && m_state == State::Reconnecting) {
                m_reconnectTimer.stop();
                setState(State::Offline);
            }
        });
    }
}

ServerConnection::~ServerConnection()
{
    m_wantConnected = false;
    teardownSocket();
}

QString ServerConnection::stateName(State s)
{
    switch (s) {
    case State::NotConfigured:
        return QStringLiteral("not_configured");
    case State::Disconnected:
        return QStringLiteral("disconnected");
    case State::Connecting:
        return QStringLiteral("connecting");
    case State::Authenticating:
        return QStringLiteral("authenticating");
    case State::LoginRequired:
        return QStringLiteral("login_required");
    case State::Synchronizing:
        return QStringLiteral("synchronizing");
    case State::Connected:
        return QStringLiteral("connected");
    case State::Reconnecting:
        return QStringLiteral("reconnecting");
    case State::Offline:
        return QStringLiteral("offline");
    case State::Error:
        return QStringLiteral("error");
    }
    return QStringLiteral("unknown");
}

int ServerConnection::reconnectInMs() const
{
    return m_reconnectTimer.isActive() ? m_reconnectTimer.remainingTime() : 0;
}

QString ServerConnection::refreshKey() const
{
    return QStringLiteral("refresh/%1").arg(m_account.id);
}

void ServerConnection::setState(State s, const QString& code, const QString& message)
{
    const bool changed = s != m_state || code != m_errorCode || message != m_errorMessage;
    m_state = s;
    if (s == State::Error || !code.isEmpty() || !message.isEmpty()) {
        m_errorCode = code;
        m_errorMessage = message;
    } else if (s == State::Connected) {
        m_errorCode.clear();
        m_errorMessage.clear();
    }
    if (changed) {
        OMA_INFO("net", "connection state", {"state", stateName(s)}, {"code", m_errorCode});
        emit stateChanged();
    }
}

void ServerConnection::start(const Account& account, Credentials credentials)
{
    const bool sameAccount = account.id == m_account.id;
    teardownSocket();
    failPending(QStringLiteral("connection restarted"));
    if (!sameAccount) {
        m_model.clear();
        m_accessToken.clear();
        m_sessionId = 0;
    }
    m_account = account;
    m_pendingCredentials = std::move(credentials);
    m_certFingerprint.clear();
    m_wantConnected = true;
    m_backoff.reset();
    m_reconnectTimer.stop();
    connectNow();
}

void ServerConnection::stop()
{
    m_wantConnected = false;
    m_reconnectTimer.stop();
    teardownSocket();
    failPending(QStringLiteral("disconnected"));
    setState(m_account.id ? State::Disconnected : State::NotConfigured);
}

void ServerConnection::teardownSocket()
{
    ++m_generation;
    m_connectTimeout.stop();
    if (m_conn) {
        m_conn->disconnect(this);
        if (m_socket)
            m_socket->disconnect(this);
        m_conn->abort();
        m_conn->deleteLater();
    }
    m_conn = nullptr;
    m_socket = nullptr;
    m_syncInFlight = false;
}

void ServerConnection::connectNow()
{
    if (!m_wantConnected || m_account.id == 0)
        return;
    teardownSocket();
    // After a successful session, attempts are reported as "reconnecting" so
    // the UI keeps showing the (stale) model instead of a blank screen.
    setState(m_everConnected && m_model.valid() ? State::Reconnecting : State::Connecting);

    auto* socket = new QSslSocket();
    QSslConfiguration tls = QSslConfiguration::defaultConfiguration();
    tls.setProtocol(QSsl::TlsV1_3OrLater);
    tls.setPeerVerifyMode(QSslSocket::VerifyPeer);
    socket->setSslConfiguration(tls);
    socket->setPeerVerifyName(m_account.host);

    m_socket = socket;
    m_conn = new protocol::ProtocolConnection(socket, this);
    const auto gen = m_generation;

    connect(socket, &QSslSocket::connected, this, [this, gen] {
        if (gen == m_generation && m_socket)
            enableTcpKeepalive(m_socket->socketDescriptor());
    });
    connect(socket, &QSslSocket::sslErrors, this, [this, gen](const QList<QSslError>& errors) {
        if (gen != m_generation || !m_socket)
            return;
        const QSslCertificate peer = m_socket->peerCertificate();
        const QString fp = peer.isNull() ? QString() : fingerprintOf(peer);
        if (!fp.isEmpty() && fp == m_account.trustedFingerprint) {
            // Explicitly trusted by the user for this account (self-signed
            // or private CA). Only the exact pinned certificate is accepted.
            m_socket->ignoreSslErrors(errors);
            return;
        }
        QStringList reasons;
        for (const auto& e : errors)
            reasons << e.errorString();
        m_certFingerprint = fp;
        OMA_WARN("net", "certificate not trusted", {"host", m_account.host}, {"fingerprint", fp},
            {"errors", reasons.join(QStringLiteral("; "))});
        m_wantConnected = false;
        const QString msg
            = QStringLiteral("The server certificate is not trusted: %1").arg(reasons.join(QStringLiteral("; ")));
        setState(State::Error, ipc::errors::CertificateError, msg);
        failCredentials(ipc::errors::CertificateError, msg);
    });
    connect(socket, &QSslSocket::encrypted, this, [this, gen] {
        if (gen == m_generation)
            onEncrypted();
    });
    connect(socket, &QSslSocket::errorOccurred, this, [this, gen](QAbstractSocket::SocketError) {
        if (gen == m_generation && m_socket && m_state != State::Error)
            m_errorMessage = m_socket->errorString();
    });
    connect(m_conn, &protocol::ProtocolConnection::envelopeReceived, this, [this, gen](const proto::Envelope& env) {
        if (gen == m_generation)
            onEnvelope(env);
    });
    connect(m_conn, &protocol::ProtocolConnection::closed, this, [this, gen] {
        if (gen == m_generation)
            onSocketClosed();
    });

    m_connectTimeout.start();
    OMA_INFO("net", "connecting", {"host", m_account.host}, {"port", m_account.port});
    socket->connectToHostEncrypted(m_account.host, m_account.port);
}

void ServerConnection::onEncrypted()
{
    m_connectTimeout.stop();
    m_serverAddress = m_socket->peerAddress();
    proto::Envelope env;
    auto* hello = env.mutable_hello();
    hello->set_protocol_major(kProtocolMajor);
    hello->set_protocol_minor(kProtocolMinor);
    hello->set_client_version(kVersion);
    hello->add_capabilities("resume");
    hello->add_capabilities("voice.opus");
    setState(State::Authenticating);
    request(std::move(env), [this](const proto::Envelope& reply) {
        if (reply.has_error()) {
            const bool mismatch = reply.error().code() == proto::ERROR_PROTOCOL_MISMATCH;
            m_wantConnected = !mismatch && m_wantConnected;
            setState(mismatch ? State::Error : State::Reconnecting,
                mismatch ? ipc::errors::ProtocolMismatch : ipc::errors::NetworkError,
                QString::fromStdString(reply.error().message()));
            if (mismatch)
                failCredentials(ipc::errors::ProtocolMismatch, QString::fromStdString(reply.error().message()));
            else
                scheduleReconnect();
            return;
        }
        const auto& hr = reply.hello_reply();
        m_instanceName = QString::fromStdString(hr.instance_name());
        m_mediaPort = static_cast<quint16>(hr.media_udp_port());
        m_maxUploadBytes = hr.max_upload_bytes();
        m_capabilities.clear();
        for (const auto& c : hr.capabilities())
            m_capabilities << QString::fromStdString(c);
        beginAuth();
    });
}

void ServerConnection::beginAuth()
{
    if (m_pendingCredentials.kind != Credentials::Kind::None) {
        sendCredentials();
        return;
    }
    if (!m_accessToken.isEmpty() && m_sessionId && m_model.valid()
        && QDateTime::currentMSecsSinceEpoch() < m_accessExpiresAt) {
        sendResume();
        return;
    }
    const auto gen = m_generation;
    m_credentials.read(refreshKey(), [this, gen](bool ok, const QString& token, const QString& error) {
        if (gen != m_generation)
            return;
        if (!ok) {
            OMA_WARN("auth", "keyring unavailable", {"error", error});
        }
        if (token.isEmpty()) {
            setState(State::LoginRequired);
            return;
        }
        sendRefresh(token);
    });
}

void ServerConnection::sendResume()
{
    proto::Envelope env;
    auto* r = env.mutable_resume();
    r->set_access_token(m_accessToken.toStdString());
    r->set_session_id(m_sessionId);
    r->set_last_sequence(m_model.lastSequence());
    request(std::move(env), [this](const proto::Envelope& reply) {
        if (!reply.has_error()) {
            OMA_INFO("net", "session resumed", {"replayed", reply.resume_result().replayed_events()});
            m_backoff.reset();
            setState(State::Connected);
            emit resumed();
            return;
        }
        if (reply.error().code() == proto::ERROR_RESUME_FAILED) {
            resync();
            return;
        }
        // Access token no longer valid: fall back to the refresh token.
        m_accessToken.clear();
        beginAuth();
    });
}

void ServerConnection::sendRefresh(const QString& token)
{
    proto::Envelope env;
    env.mutable_refresh()->set_refresh_token(token.toStdString());
    request(std::move(env), [this](const proto::Envelope& reply) {
        if (reply.has_error() && reply.error().code() == proto::ERROR_AUTHENTICATION) {
            m_credentials.remove(refreshKey(), {});
            setState(State::LoginRequired, ipc::errors::AuthenticationError,
                QString::fromStdString(reply.error().message()));
            return;
        }
        handleAuthReply(reply);
    });
}

void ServerConnection::login(Credentials credentials)
{
    m_pendingCredentials = std::move(credentials);
    if (m_state == State::LoginRequired && m_conn) {
        sendCredentials();
        return;
    }
    // Not currently at the login prompt: restart the handshake.
    m_wantConnected = true;
    m_backoff.reset();
    connectNow();
}

void ServerConnection::sendCredentials()
{
    Credentials creds = std::move(m_pendingCredentials);
    m_pendingCredentials = {};
    proto::Envelope env;
    if (creds.kind == Credentials::Kind::Register) {
        auto* r = env.mutable_register_();
        r->set_username(creds.username.toStdString());
        r->set_password(creds.password.toStdString());
        r->set_display_name(creds.displayName.toStdString());
    } else {
        auto* l = env.mutable_login();
        l->set_username(creds.username.toStdString());
        l->set_password(creds.password.toStdString());
    }
    setState(State::Authenticating);
    auto done = std::move(creds.done);
    request(
        std::move(env),
        [this, done](const proto::Envelope& reply) {
            if (reply.has_error()) {
                const QString code = reply.error().code() == proto::ERROR_RATE_LIMITED
                    ? ipc::errors::RateLimited
                    : ipcErrorCode(reply.error().code());
                const QString msg = QString::fromStdString(reply.error().message());
                setState(State::LoginRequired, code, msg);
                if (done)
                    done(false, code, msg);
                return;
            }
            handleAuthReply(reply);
            if (done)
                done(true, QString(), QString());
        },
        30000);
}

void ServerConnection::handleAuthReply(const proto::Envelope& reply)
{
    if (reply.has_error()) {
        setState(State::Reconnecting, ipc::errors::NetworkError, QString::fromStdString(reply.error().message()));
        scheduleReconnect();
        return;
    }
    const auto& r = reply.auth_result();
    m_accessToken = QString::fromStdString(r.access_token());
    m_sessionId = r.session_id();
    m_accessExpiresAt = r.access_expires_at();
    if (!r.refresh_token().empty()) {
        m_credentials.write(refreshKey(), QString::fromStdString(r.refresh_token()), [](bool ok, const QString& err) {
            if (!ok)
                OMA_WARN("auth", "could not store session in keyring; you will need to log in again", {"error", err});
        });
    }
    emit authenticated(r.user().id());
    resync();
}

void ServerConnection::resync(std::function<void(bool)> done)
{
    if (!m_conn) {
        if (done)
            done(false);
        return;
    }
    if (done)
        m_syncWaiters.push_back(std::move(done));
    if (m_syncInFlight) {
        m_resyncQueued = true;
        return;
    }
    m_syncInFlight = true;
    if (m_state != State::Connected)
        setState(State::Synchronizing);
    proto::Envelope env;
    env.mutable_sync();
    request(
        std::move(env),
        [this](const proto::Envelope& reply) {
            m_syncInFlight = false;
            if (reply.has_error()) {
                OMA_WARN("net", "synchronization failed", {"error", QString::fromStdString(reply.error().message())});
                for (auto& w : std::exchange(m_syncWaiters, {}))
                    w(false);
                if (m_conn)
                    m_conn->abort();
                return;
            }
            m_model.reset(reply.sync_state());
            m_everConnected = true;
            m_backoff.reset();
            setState(State::Connected);
            emit synchronized();
            if (m_resyncQueued) {
                // Waiters registered during this sync need the next one.
                m_resyncQueued = false;
                resync();
                return;
            }
            for (auto& w : std::exchange(m_syncWaiters, {}))
                w(true);
        },
        60000);
}

void ServerConnection::logout(AuthCallback done)
{
    m_credentials.remove(refreshKey(), {});
    m_accessToken.clear();
    m_sessionId = 0;
    if (m_conn && (m_state == State::Connected || m_state == State::Synchronizing)) {
        proto::Envelope env;
        env.mutable_logout();
        request(std::move(env), [this, done](const proto::Envelope&) {
            m_wantConnected = false;
            teardownSocket();
            m_model.clear();
            setState(State::Disconnected);
            if (done)
                done(true, QString(), QString());
        });
        return;
    }
    m_wantConnected = false;
    teardownSocket();
    m_model.clear();
    setState(State::Disconnected);
    if (done)
        done(true, QString(), QString());
}

void ServerConnection::request(proto::Envelope env, ReplyCallback cb, int timeoutMs)
{
    if (!m_conn) {
        proto::Envelope err;
        err.mutable_error()->set_code(proto::ERROR_INTERNAL);
        err.mutable_error()->set_message("not connected");
        if (cb)
            cb(err);
        return;
    }
    const std::uint64_t id = m_nextRequestId++;
    env.set_request_id(id);
    auto* timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, id] {
        auto it = m_pending.find(id);
        if (it == m_pending.end())
            return;
        Pending p = std::move(it->second);
        m_pending.erase(it);
        p.timer->deleteLater();
        proto::Envelope err;
        err.mutable_error()->set_code(proto::ERROR_INTERNAL);
        err.mutable_error()->set_message("server did not answer in time");
        if (p.cb)
            p.cb(err);
    });
    timer->start(timeoutMs);
    m_pending.emplace(id, Pending{std::move(cb), timer});
    m_conn->send(env);
}

void ServerConnection::failPending(const QString& message)
{
    for (auto& w : std::exchange(m_syncWaiters, {}))
        w(false);
    auto pending = std::exchange(m_pending, {});
    for (auto& [id, p] : pending) {
        p.timer->stop();
        p.timer->deleteLater();
        proto::Envelope err;
        err.mutable_error()->set_code(proto::ERROR_INTERNAL);
        err.mutable_error()->set_message(message.toStdString());
        if (p.cb)
            p.cb(err);
    }
}

void ServerConnection::onEnvelope(const proto::Envelope& env)
{
    if (env.has_event()) {
        std::vector<ModelEvent> events;
        bool needsResync = false;
        m_model.apply(env.event(), events, needsResync);
        for (const auto& e : events)
            emit modelEvent(e.name, e.data);
        if (needsResync)
            resync();
        return;
    }
    auto it = m_pending.find(env.request_id());
    if (it == m_pending.end())
        return;
    Pending p = std::move(it->second);
    m_pending.erase(it);
    p.timer->stop();
    p.timer->deleteLater();
    if (p.cb)
        p.cb(env);
}

void ServerConnection::dropLink(const QString& reason)
{
    if (!m_conn)
        return;
    m_errorMessage = reason;
    onSocketClosed();
}

void ServerConnection::onSocketClosed()
{
    m_connectTimeout.stop();
    const QString reason = m_errorMessage;
    teardownSocket();
    failPending(QStringLiteral("connection lost"));
    if (m_state == State::Error || !m_wantConnected)
        return;
    if (m_pendingCredentials.kind != Credentials::Kind::None) {
        // An explicit login gets a prompt answer instead of silent retries.
        m_wantConnected = false;
        const QString msg = reason.isEmpty() ? QStringLiteral("cannot reach the server") : reason;
        setState(State::Error, ipc::errors::ServerUnavailable, msg);
        failCredentials(ipc::errors::ServerUnavailable, msg);
        return;
    }
    OMA_INFO("net", "connection closed", {"reason", reason});
    scheduleReconnect();
}

void ServerConnection::failCredentials(const QString& code, const QString& message)
{
    auto creds = std::exchange(m_pendingCredentials, {});
    if (creds.done)
        creds.done(false, code, message);
}

void ServerConnection::scheduleReconnect()
{
    if (!m_wantConnected)
        return;
    auto* info = QNetworkInformation::instance();
    if (info && info->reachability() == QNetworkInformation::Reachability::Disconnected) {
        setState(State::Offline, ipc::errors::NetworkError, QStringLiteral("no network connection"));
        return;
    }
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    const int delay = m_backoff.nextDelayMs([&] { return dist(m_rng); });
    m_reconnectTimer.start(delay);
    setState(State::Reconnecting, ipc::errors::ServerUnavailable,
        m_errorMessage.isEmpty() ? QStringLiteral("connection lost") : m_errorMessage);
    OMA_INFO("net", "reconnect scheduled", {"delay_ms", delay}, {"attempt", m_backoff.attempts()});
}

} // namespace omachat::daemon
