#pragma once

#include "application/Daemon.hpp"
#include "core/ChatServer.hpp"
#include "network.pb.h"
#include "omachat/ipc/IpcClient.hpp"
#include "omachat/protocol/ProtocolConnection.hpp"

#include <QTemporaryDir>

#include <deque>
#include <memory>
#include <optional>

namespace omachat::test {

// A real omachat-server on 127.0.0.1 with an ephemeral self-signed
// certificate and a temporary SQLite database.
class TestServer {
public:
    TestServer();
    ~TestServer();

    bool start(quint16 port = 0, quint16 mediaPort = 0);
    void setOperatorUsername(const QString& username) { m_operatorUsername = username; }
    void stop();
    quint16 port() const { return m_port; }
    quint16 mediaPort() const { return m_mediaPort; }
    const QSslCertificate& certificate() const { return m_identity.chain.first(); }
    QString fingerprint() const;
    server::ChatServer* server() { return m_server.get(); }

private:
    QTemporaryDir m_dir;
    server::TlsIdentity m_identity;
    std::unique_ptr<server::ChatServer> m_server;
    quint16 m_port = 0;
    quint16 m_mediaPort = 0;
    QString m_operatorUsername;
};

// Speaks the protobuf protocol directly (no daemon).
class RawClient {
public:
    explicit RawClient(const TestServer& server);
    ~RawClient();

    bool connect();
    bool connected() const;
    // Sends and waits for the reply with the same request id.
    std::optional<proto::Envelope> call(proto::Envelope env, int timeoutMs = 10000);
    // Waits until an event matching `pred` arrives (consumed from the queue).
    std::optional<proto::Event> waitEvent(const std::function<bool(const proto::Event&)>& pred, int timeoutMs = 5000);
    void clearEvents() { m_events.clear(); }
    void abort();

    // Convenience flows.
    bool hello();
    std::optional<proto::AuthResult> registerUser(const std::string& name, const std::string& password);
    std::optional<proto::AuthResult> login(const std::string& name, const std::string& password);

private:
    const TestServer& m_server;
    protocol::ProtocolConnection* m_conn = nullptr;
    std::deque<proto::Envelope> m_replies;
    std::deque<proto::Event> m_events;
    std::uint64_t m_nextId = 1;
    bool m_closed = false;
};

// A full omachatd in-process, with its own temp dir, memory credentials and
// the null audio backend, driven through a real IPC socket.
class TestDaemon {
public:
    explicit TestDaemon(const QString& name);
    ~TestDaemon();

    bool start();
    ipc::Reply call(const QString& method, const QJsonObject& params = {}, int timeoutMs = 15000);
    std::optional<QJsonObject> waitEvent(
        const QString& name, const std::function<bool(const QJsonObject&)>& pred = {}, int timeoutMs = 5000);
    bool waitState(const QString& state, int timeoutMs = 10000);

    // Registers (trusting the certificate on first contact) and waits until connected.
    bool registerOn(const TestServer& server, const QString& username, const QString& password);
    // Same, for an account that already exists (a second device).
    bool loginOn(const TestServer& server, const QString& username, const QString& password);

    daemon::Daemon& daemon() { return *m_daemon; }
    audio::NullAudioBackend& audio();
    ipc::IpcClient& ipc() { return m_ipc; }

private:
    bool authOn(const TestServer& server, const QString& username, const QString& password, const QString& method);

    QTemporaryDir m_dir;
    QString m_socket;
    std::unique_ptr<daemon::Daemon> m_daemon;
    ipc::IpcClient m_ipc;
    std::deque<std::pair<QString, QJsonObject>> m_events;
};

// Pumps the Qt event loop until `pred` holds or the timeout expires.
bool waitFor(const std::function<bool()>& pred, int timeoutMs = 5000);

} // namespace omachat::test
