#include "Harness.hpp"

#include "omachat/core/Version.hpp"
#include "storage/Store.hpp"
#include "transport/Certificates.hpp"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QSslConfiguration>
#include <QTest>

namespace omachat::test {

bool waitFor(const std::function<bool()>& pred, int timeoutMs)
{
    return QTest::qWaitFor(pred, QDeadlineTimer(timeoutMs));
}

// ---------------------------------------------------------------- server

TestServer::TestServer()
{
    QString error;
    server::generateSelfSigned(m_dir.filePath(QStringLiteral("cert.pem")), m_dir.filePath(QStringLiteral("key.pem")),
        {QStringLiteral("localhost"), QStringLiteral("127.0.0.1")}, 30, &error);
    server::loadTlsIdentity(
        m_dir.filePath(QStringLiteral("cert.pem")), m_dir.filePath(QStringLiteral("key.pem")), m_identity, &error);
}

TestServer::~TestServer()
{
    stop();
}

bool TestServer::start(quint16 port, quint16 mediaPort)
{
    server::ServerConfig cfg;
    cfg.instanceName = QStringLiteral("Test Instance");
    cfg.bind = QStringLiteral("127.0.0.1");
    cfg.mediaBind = QStringLiteral("127.0.0.1");
    cfg.port = port;
    cfg.mediaPort = mediaPort;
    cfg.databasePath = m_dir.filePath(QStringLiteral("server.db"));
    cfg.filesPath = m_dir.filePath(QStringLiteral("files"));
    cfg.maxConnectionsPerIp = 1000;
    cfg.operatorUserId = m_operatorUserId;
    m_server = std::make_unique<server::ChatServer>(cfg);
    QString error;
    if (!m_server->start(m_identity, &error)) {
        qWarning("test server failed: %s", qPrintable(error));
        return false;
    }
    m_port = m_server->controlPort();
    m_mediaPort = m_server->mediaPort();
    return true;
}

void TestServer::stop()
{
    m_server.reset();
}

server::Id TestServer::userId(const QString& username)
{
    server::Store store;
    QString error;
    if (!store.open(m_dir.filePath(QStringLiteral("server.db")), &error))
        return 0;
    const auto user = store.userByName(username);
    return user ? user->id : 0;
}

QString TestServer::fingerprint() const
{
    return server::fingerprint(certificate());
}

// ------------------------------------------------------------ raw client

RawClient::RawClient(const TestServer& server)
    : m_server(server)
{
}

RawClient::~RawClient()
{
    if (m_conn) {
        m_conn->disconnect();
        delete m_conn;
    }
}

bool RawClient::connect()
{
    auto* socket = new QSslSocket();
    QSslConfiguration tls = QSslConfiguration::defaultConfiguration();
    tls.setCaCertificates({m_server.certificate()});
    tls.setProtocol(QSsl::TlsV1_3OrLater);
    socket->setSslConfiguration(tls);
    m_conn = new protocol::ProtocolConnection(socket);
    QObject::connect(m_conn, &protocol::ProtocolConnection::envelopeReceived, [this](const proto::Envelope& e) {
        if (m_dropNextReply && e.request_id() != 0) {
            m_dropNextReply = false;
            m_conn->abort();
            return;
        }
        if (e.has_event())
            m_events.push_back(e.event());
        else
            m_replies.push_back(e);
    });
    QObject::connect(m_conn, &protocol::ProtocolConnection::closed, [this] { m_closed = true; });
    socket->connectToHostEncrypted(QStringLiteral("127.0.0.1"), m_server.port());
    return waitFor([socket] { return socket->isEncrypted(); }, 5000);
}

bool RawClient::connected() const
{
    return m_conn && !m_closed;
}

void RawClient::abort()
{
    if (m_conn)
        m_conn->abort();
}

std::optional<proto::Envelope> RawClient::callDroppingReply(proto::Envelope env)
{
    m_dropNextReply = true;
    return call(std::move(env));
}

std::optional<proto::Envelope> RawClient::call(proto::Envelope env, int timeoutMs)
{
    const std::uint64_t id = m_nextId++;
    env.set_request_id(id);
    if (!m_conn->send(env))
        return std::nullopt;
    std::optional<proto::Envelope> result;
    waitFor(
        [&] {
            for (auto it = m_replies.begin(); it != m_replies.end(); ++it) {
                if (it->request_id() == id) {
                    result = *it;
                    m_replies.erase(it);
                    return true;
                }
            }
            return m_closed;
        },
        timeoutMs);
    return result;
}

std::optional<proto::Event> RawClient::waitEvent(const std::function<bool(const proto::Event&)>& pred, int timeoutMs)
{
    std::optional<proto::Event> found;
    waitFor(
        [&] {
            for (auto it = m_events.begin(); it != m_events.end(); ++it) {
                if (pred(*it)) {
                    found = *it;
                    m_events.erase(it);
                    return true;
                }
            }
            return false;
        },
        timeoutMs);
    return found;
}

bool RawClient::hello()
{
    proto::Envelope env;
    env.mutable_hello()->set_protocol_major(kProtocolMajor);
    env.mutable_hello()->set_protocol_minor(kProtocolMinor);
    auto r = call(env);
    return r && r->has_hello_reply();
}

std::optional<proto::AuthResult> RawClient::registerUser(const std::string& name, const std::string& password)
{
    proto::Envelope env;
    env.mutable_register_()->set_username(name);
    env.mutable_register_()->set_password(password);
    auto r = call(env, 20000);
    if (!r || !r->has_auth_result())
        return std::nullopt;
    return r->auth_result();
}

std::optional<proto::AuthResult> RawClient::login(const std::string& name, const std::string& password)
{
    proto::Envelope env;
    env.mutable_login()->set_username(name);
    env.mutable_login()->set_password(password);
    auto r = call(env, 20000);
    if (!r || !r->has_auth_result())
        return std::nullopt;
    return r->auth_result();
}

// ---------------------------------------------------------------- daemon

TestDaemon::TestDaemon(const QString& name)
{
    // Keep the socket path short: sun_path is limited to 108 bytes.
    m_socket = QStringLiteral("/tmp/omachat-test-%1-%2.sock").arg(name).arg(QCoreApplication::applicationPid());
    QObject::connect(&m_ipc, &ipc::IpcClient::eventReceived, [this](const QString& n, const QJsonObject& d) {
        m_events.emplace_back(n, d);
        if (m_events.size() > 5000)
            m_events.pop_front();
    });
}

TestDaemon::~TestDaemon()
{
    m_ipc.disconnectFromDaemon();
    if (m_daemon)
        m_daemon->shutdown();
}

bool TestDaemon::start()
{
    daemon::DaemonOptions opt;
    opt.socketPath = m_socket;
    opt.databasePath = m_dir.filePath(QStringLiteral("local.db"));
    opt.configPath = m_dir.filePath(QStringLiteral("config.toml"));
    opt.memoryCredentials = true;
    opt.nullAudio = true;
    opt.syntheticScreen = true;
    opt.notifications = false;
    m_daemon = std::make_unique<daemon::Daemon>(opt);
    QString error;
    if (!m_daemon->start(&error)) {
        qWarning("daemon failed: %s", qPrintable(error));
        return false;
    }
    m_ipc.connectToDaemon(m_socket);
    if (!waitFor([this] { return m_ipc.isConnected(); }, 3000))
        return false;
    return call(QStringLiteral("events.subscribe")).ok;
}

audio::NullAudioBackend& TestDaemon::audio()
{
    return static_cast<audio::NullAudioBackend&>(m_daemon->audioBackend());
}

ipc::Reply TestDaemon::call(const QString& method, const QJsonObject& params, int timeoutMs)
{
    std::optional<ipc::Reply> reply;
    m_ipc.request(method, params, [&](const ipc::Reply& r) { reply = r; }, timeoutMs);
    waitFor([&] { return reply.has_value(); }, timeoutMs + 1000);
    if (!reply) {
        ipc::Reply r;
        r.errorCode = QStringLiteral("TestTimeout");
        return r;
    }
    return *reply;
}

std::optional<QJsonObject> TestDaemon::waitEvent(
    const QString& name, const std::function<bool(const QJsonObject&)>& pred, int timeoutMs)
{
    std::optional<QJsonObject> found;
    waitFor(
        [&] {
            for (auto it = m_events.begin(); it != m_events.end(); ++it) {
                if (it->first == name && (!pred || pred(it->second))) {
                    found = it->second;
                    m_events.erase(it);
                    return true;
                }
            }
            return false;
        },
        timeoutMs);
    return found;
}

bool TestDaemon::waitState(const QString& state, int timeoutMs)
{
    return waitFor(
        [&] { return m_daemon->statusJson().value(QStringLiteral("state")).toString() == state; }, timeoutMs);
}

bool TestDaemon::registerOn(const TestServer& server, const QString& username, const QString& password)
{
    return authOn(server, username, password, QStringLiteral("account.register"));
}

bool TestDaemon::loginOn(const TestServer& server, const QString& username, const QString& password)
{
    return authOn(server, username, password, QStringLiteral("account.login"));
}

bool TestDaemon::authOn(
    const TestServer& server, const QString& username, const QString& password, const QString& method)
{
    const QJsonObject creds{{"host", "127.0.0.1"}, {"port", server.port()}, {"username", username},
        {"password", password}, {"display_name", username.toUpper()}};
    auto first = call(method, creds, 20000);
    if (first.ok)
        return waitState(QStringLiteral("connected"));
    if (first.errorCode != u"CertificateError")
        return false;
    const QString fp = m_daemon->statusJson()
                           .value(QStringLiteral("error"))
                           .toObject()
                           .value(QStringLiteral("fingerprint"))
                           .toString();
    if (fp != server.fingerprint())
        return false;
    if (!call(QStringLiteral("certificate.trust"), {{"fingerprint", fp}}).ok)
        return false;
    if (!waitState(QStringLiteral("login_required")))
        return false;
    auto second = call(method, creds, 20000);
    return second.ok && waitState(QStringLiteral("connected"));
}

} // namespace omachat::test
