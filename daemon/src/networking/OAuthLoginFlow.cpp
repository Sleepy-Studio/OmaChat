#include "networking/OAuthLoginFlow.hpp"

#include "omachat/core/Log.hpp"
#include "omachat/ipc/IpcMessage.hpp"

#include <QCryptographicHash>
#include <QHostAddress>
#include <QProcess>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

namespace omachat::daemon {
namespace {

namespace e = ipc::errors;

// Unpadded base64url, as PKCE (RFC 7636) and OAuth state parameters expect.
QString randomUrlSafeToken(int bytes)
{
    const int words = bytes / 4 + 1;
    QByteArray buf(words * 4, Qt::Uninitialized);
    QRandomGenerator::global()->fillRange(reinterpret_cast<quint32*>(buf.data()), words);
    return QString::fromLatin1(
        buf.left(bytes).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString pkceChallenge(const QString& verifier)
{
    const auto digest = QCryptographicHash::hash(verifier.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

} // namespace

void OAuthLoginFlow::start(ServerConnection& conn, proto::OAuthProvider provider, Mode mode, Done done)
{
    // Owns itself; `finish()` calls deleteLater() exactly once.
    auto* flow = new OAuthLoginFlow(conn, provider, mode, std::move(done));
    flow->awaitLoginPrompt();
}

OAuthLoginFlow::OAuthLoginFlow(ServerConnection& conn, proto::OAuthProvider provider, Mode mode, Done done)
    : m_conn(conn)
    , m_provider(provider)
    , m_mode(mode)
    , m_done(std::move(done))
{
    m_timeout.setSingleShot(true);
}

void OAuthLoginFlow::awaitLoginPrompt()
{
    const auto wanted
        = m_mode == Mode::Login ? ServerConnection::State::LoginRequired : ServerConnection::State::Connected;
    if (m_conn.state() == wanted) {
        beginBrowserFlow();
        return;
    }
    connect(&m_timeout, &QTimer::timeout, this,
        [this] { finish(false, e::ServerUnavailable, QStringLiteral("could not reach the server")); });
    m_timeout.start(15000);
    connect(&m_conn, &ServerConnection::stateChanged, this, [this, wanted] {
        const auto s = m_conn.state();
        if (s == wanted) {
            m_timeout.stop();
            disconnect(&m_conn, &ServerConnection::stateChanged, this, nullptr);
            beginBrowserFlow();
            return;
        }
        // Still on the way to `wanted` (checked above and already handled).
        const bool stillOnTheWay = s == ServerConnection::State::Connecting
            || s == ServerConnection::State::Authenticating || s == ServerConnection::State::Synchronizing;
        if (stillOnTheWay)
            return;
        m_timeout.stop();
        disconnect(&m_conn, &ServerConnection::stateChanged, this, nullptr);
        // An untrusted certificate is not a dead end: the same account can
        // be resumed once the caller trusts it, exactly like a normal login
        // would. Anything else here is unrecoverable for this attempt.
        const QString code = m_conn.errorCode().isEmpty() ? e::ServerUnavailable : m_conn.errorCode();
        finish(false, code, m_conn.errorMessage());
    });
}

void OAuthLoginFlow::beginBrowserFlow()
{
    const OAuthProviderInfo* meta = nullptr;
    for (const auto& p : m_conn.oauthProviders()) {
        if (p.provider == m_provider) {
            meta = &p;
            break;
        }
    }
    if (!meta) {
        finish(false, e::BadRequest, QStringLiteral("that sign-in method is not enabled on this server"));
        return;
    }

    m_server = new QTcpServer(this);
    // A fixed port, not an OS-assigned ephemeral one: Discord and GitHub
    // validate the redirect_uri with an exact string match (Google is the
    // only one of the three with documented loopback-any-port support per
    // RFC 8252), so whatever port we use has to be the one operators
    // registered with each provider. See docs/self-hosting.md.
    if (!m_server->listen(QHostAddress::LocalHost, kRedirectPort)) {
        finish(false, e::Internal,
            QStringLiteral("could not listen on 127.0.0.1:%1 for the sign-in redirect "
                           "(something else may be using that port)")
                .arg(kRedirectPort));
        return;
    }
    connect(m_server, &QTcpServer::newConnection, this, &OAuthLoginFlow::onLoopbackConnection);

    m_redirectUri = QStringLiteral("http://127.0.0.1:%1/callback").arg(m_server->serverPort());
    m_codeVerifier = randomUrlSafeToken(64);
    m_state = randomUrlSafeToken(16);

    QUrl url(meta->authorizeUrl);
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("client_id"), meta->clientId);
    q.addQueryItem(QStringLiteral("redirect_uri"), m_redirectUri);
    q.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    q.addQueryItem(QStringLiteral("scope"), meta->scope);
    q.addQueryItem(QStringLiteral("state"), m_state);
    q.addQueryItem(QStringLiteral("code_challenge"), pkceChallenge(m_codeVerifier));
    q.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    url.setQuery(q);

    // QDesktopServices::openUrl needs a QGuiApplication to reliably shell
    // out on Linux; omachatd is a QCoreApplication (headless daemon), so it
    // can silently refuse even with a perfectly good desktop session
    // present. Launching xdg-open directly has no such requirement.
    if (!QProcess::startDetached(QStringLiteral("xdg-open"), {url.toString()})) {
        finish(false, e::Internal, QStringLiteral("could not open your browser (is xdg-utils installed?)"));
        return;
    }

    connect(&m_timeout, &QTimer::timeout, this,
        [this] { finish(false, e::Timeout, QStringLiteral("sign-in was not completed in time")); });
    m_timeout.start(5 * 60 * 1000);
}

void OAuthLoginFlow::onLoopbackConnection()
{
    QTcpSocket* sock = m_server->nextPendingConnection();
    if (!sock)
        return;
    connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
        // The request line is all that matters; wait for at least one CRLF.
        if (!sock->canReadLine())
            return;
        const QByteArray line = sock->readLine();
        const QList<QByteArray> parts = line.split(' ');
        const QString target = parts.size() >= 2 ? QString::fromLatin1(parts[1]) : QString();
        static const QByteArray kBody
            = "<!doctype html><html><body style=\"font:15px sans-serif;padding:2em\">"
              "Authorization received. Return to OmaChat to see whether sign-in or linking completed.</body></html>";
        QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: "
            + QByteArray::number(kBody.size()) + "\r\nConnection: close\r\n\r\n" + kBody;
        sock->write(response);
        sock->flush();
        sock->disconnectFromHost();
        sock->deleteLater();
        m_server->close();
        handleCallback(target);
    });
}

void OAuthLoginFlow::handleCallback(const QString& target)
{
    const QUrl url(QStringLiteral("http://127.0.0.1") + target);
    const QUrlQuery q(url.query());
    const QString error = q.queryItemValue(QStringLiteral("error"));
    if (!error.isEmpty()) {
        finish(false, e::AuthenticationError, QStringLiteral("sign-in was cancelled"));
        return;
    }
    if (q.queryItemValue(QStringLiteral("state")) != m_state) {
        finish(false, e::AuthenticationError, QStringLiteral("sign-in response did not match this attempt"));
        return;
    }
    const QString code = q.queryItemValue(QStringLiteral("code"));
    if (code.isEmpty()) {
        finish(false, e::AuthenticationError, QStringLiteral("no authorization code was returned"));
        return;
    }
    m_timeout.stop();
    exchangeCode(code);
}

void OAuthLoginFlow::exchangeCode(const QString& code)
{
    if (m_mode == Mode::Login) {
        ServerConnection::Credentials creds;
        creds.kind = ServerConnection::Credentials::Kind::OAuth;
        creds.oauthProvider = m_provider;
        creds.oauthCode = code;
        creds.oauthCodeVerifier = m_codeVerifier;
        creds.oauthRedirectUri = m_redirectUri;
        creds.done = [this](bool ok, const QString& errCode, const QString& message) { finish(ok, errCode, message); };
        m_conn.login(std::move(creds));
        return;
    }
    // Link: the connection is already authenticated, so this is a plain
    // request/reply over the existing session rather than an auth attempt.
    proto::Envelope env;
    auto* link = env.mutable_oauth_link();
    link->set_provider(m_provider);
    link->set_code(code.toStdString());
    link->set_code_verifier(m_codeVerifier.toStdString());
    link->set_redirect_uri(m_redirectUri.toStdString());
    m_conn.request(
        std::move(env),
        [this](const proto::Envelope& reply) {
            if (reply.has_error()) {
                finish(false, ipcErrorCode(reply.error().code()), QString::fromStdString(reply.error().message()));
                return;
            }
            finish(true, QString(), QString());
        },
        30000);
}

void OAuthLoginFlow::finish(bool ok, const QString& code, const QString& message)
{
    if (m_finished)
        return;
    m_finished = true;
    m_timeout.stop();
    if (m_server)
        m_server->close();
    if (m_done)
        m_done(ok, code, message);
    deleteLater();
}

} // namespace omachat::daemon
