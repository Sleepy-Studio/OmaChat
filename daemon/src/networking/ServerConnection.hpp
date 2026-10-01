#pragma once

#include "network.pb.h"
#include "networking/ClientState.hpp"
#include "omachat/core/RateLimiter.hpp"
#include "omachat/protocol/ProtocolConnection.hpp"
#include "platform/CredentialStore.hpp"
#include "storage/LocalStore.hpp"

#include <QHostAddress>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <map>
#include <random>

namespace omachat::daemon {

// Maps a server error to the stable local IPC error code vocabulary.
QString ipcErrorCode(proto::ErrorCode code);

// Explicit credentials for a login or registration attempt.
struct AuthCredentials {
    enum class Kind { None, Login, Register, OAuth } kind = Kind::None;
    QString username;
    QString password;
    QString displayName;
    // OAuth only: the authorization code from the provider's redirect and
    // the PKCE verifier/redirect_uri that must match what was sent to the
    // provider's authorize endpoint.
    proto::OAuthProvider oauthProvider = proto::OAUTH_PROVIDER_UNSPECIFIED;
    QString oauthCode;
    QString oauthCodeVerifier;
    QString oauthRedirectUri;
    std::function<void(bool ok, const QString& code, const QString& message)> done;
};

// One OAuth login provider the server has advertised in its HelloReply.
struct OAuthProviderInfo {
    proto::OAuthProvider provider = proto::OAUTH_PROVIDER_UNSPECIFIED;
    QString clientId;
    QString authorizeUrl;
    QString scope;
};

// The daemon's single control connection to an OmaChat server: TLS, version
// negotiation, authentication, session resume, synchronization, reconnect.
class ServerConnection : public QObject {
    Q_OBJECT
public:
    enum class State {
        NotConfigured,
        Disconnected,
        Connecting,
        Authenticating,
        LoginRequired,
        Synchronizing,
        Connected,
        Reconnecting,
        Offline,
        Error,
    };
    Q_ENUM(State)

    using AuthCallback = std::function<void(bool ok, const QString& code, const QString& message)>;
    using ReplyCallback = std::function<void(const proto::Envelope& reply)>;

    using Credentials = AuthCredentials;

    ServerConnection(ICredentialStore& credentials, QObject* parent = nullptr);
    ~ServerConnection() override;

    // Starts (or restarts) a connection for `account`. Supplying credentials
    // performs an explicit login/registration once the handshake completes;
    // otherwise a stored refresh token is used.
    void start(const Account& account, Credentials credentials = {});
    void stop(); // disconnect without reconnecting
    // Closes the socket as a network failure would; the normal reconnect
    // (resume, transfers continuing) follows. For stale links and tests.
    void dropLink(const QString& reason);
    void login(Credentials credentials);
    void logout(AuthCallback done);
    void updateAccount(const Account& account) { m_account = account; }

    // Sends a request; the callback receives the reply envelope (check
    // `has_error()`), or a synthetic NetworkError/timeout.
    void request(proto::Envelope env, ReplyCallback cb, int timeoutMs = 15000);

    State state() const { return m_state; }
    static QString stateName(State s);
    QString errorCode() const { return m_errorCode; }
    QString errorMessage() const { return m_errorMessage; }
    QString certificateFingerprint() const { return m_certFingerprint; }
    QString instanceName() const { return m_instanceName; }
    // The username the server assigned/returned on the most recent
    // successful authentication; only meaningful right after an OAuth login
    // creates or resolves an account under a username the daemon didn't
    // choose itself.
    QString lastAuthUsername() const { return m_lastAuthUsername; }
    const Account& account() const { return m_account; }
    bool hasAccount() const { return m_account.id != 0; }
    int reconnectInMs() const;

    QHostAddress serverAddress() const { return m_serverAddress; }
    quint16 mediaPort() const { return m_mediaPort; }
    // 0 when the server does not accept attachments.
    std::uint64_t maxUploadBytes() const { return m_maxUploadBytes; }
    // What the server announced in HelloReply ("search.server", …).
    const QStringList& capabilities() const { return m_capabilities; }
    const std::vector<OAuthProviderInfo>& oauthProviders() const { return m_oauthProviders; }
    // Changes whenever the socket is torn down: a reply error that arrives
    // after it changed means "connection lost", not "the server refused".
    quint64 linkGeneration() const { return m_generation; }

    ClientState& model() { return m_model; }
    const ClientState& model() const { return m_model; }

    // Requests a full synchronization; `done` runs once the model reflects it
    // (or immediately with false if the connection is gone).
    void resync(std::function<void(bool ok)> done = {});

signals:
    void stateChanged();
    void modelEvent(const QString& name, const QJsonObject& data);
    void synchronized();
    void resumed();
    void authenticated(quint64 userId);

private:
    void connectNow();
    void onEncrypted();
    void onEnvelope(const proto::Envelope& env);
    void onSocketClosed();
    void beginAuth();
    void sendResume();
    void sendRefresh(const QString& token);
    void sendCredentials();
    void handleAuthReply(const proto::Envelope& reply, AuthCallback done = {});
    void setState(State s, const QString& code = {}, const QString& message = {});
    void scheduleReconnect();
    void failCredentials(const QString& code, const QString& message);
    void failPending(const QString& message);
    void teardownSocket();
    QString refreshKey() const;

    ICredentialStore& m_credentials;
    Account m_account;
    Credentials m_pendingCredentials;

    QPointer<protocol::ProtocolConnection> m_conn;
    QSslSocket* m_socket = nullptr;
    QTimer m_connectTimeout;
    QTimer m_reconnectTimer;
    Backoff m_backoff{500, 30000};
    std::mt19937 m_rng{std::random_device{}()};
    bool m_wantConnected = false;
    bool m_syncInFlight = false;
    bool m_resyncQueued = false;
    std::vector<std::function<void(bool)>> m_syncWaiters;
    bool m_everConnected = false;

    State m_state = State::NotConfigured;
    QString m_errorCode;
    QString m_errorMessage;
    QString m_certFingerprint;
    QString m_instanceName;
    QString m_lastAuthUsername;
    QHostAddress m_serverAddress;
    quint16 m_mediaPort = 0;
    std::uint64_t m_maxUploadBytes = 0;
    QStringList m_capabilities;
    std::vector<OAuthProviderInfo> m_oauthProviders;

    QString m_accessToken;
    quint64 m_sessionId = 0;
    std::int64_t m_accessExpiresAt = 0;

    struct Pending {
        ReplyCallback cb;
        QTimer* timer = nullptr;
    };
    std::map<std::uint64_t, Pending> m_pending;
    std::uint64_t m_nextRequestId = 1;
    std::uint64_t m_generation = 0; // invalidates callbacks from old sockets

    ClientState m_model;
};

} // namespace omachat::daemon
