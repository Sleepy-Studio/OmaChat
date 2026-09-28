#include "ipc/IpcServer.hpp"

#include "omachat/core/Log.hpp"
#include "omachat/core/Paths.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>

#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace omachat::daemon {

Responder::Responder(IpcServer* server, QLocalSocket* socket, qint64 id)
    : m_server(server)
    , m_socket(socket)
    , m_id(id)
{
}

void Responder::ok(const QJsonObject& result) const
{
    if (m_server && m_socket)
        m_server->send(m_socket, ipc::makeResult(m_id, result));
}

void Responder::error(const QString& code, const QString& message) const
{
    if (m_server && m_socket)
        m_server->send(m_socket, ipc::makeError(m_id, code, message));
}

IpcServer::IpcServer(QObject* parent)
    : QObject(parent)
{
    connect(&m_server, &QLocalServer::newConnection, this, &IpcServer::onNewConnection);
}

IpcServer::~IpcServer()
{
    close();
}

bool IpcServer::listen(const QString& path, QString* error)
{
    // sockaddr_un.sun_path holds 108 bytes including the terminator.
    if (QFile::encodeName(path).size() > 107) {
        if (error)
            *error = QStringLiteral("socket path is too long for a Unix socket (%1 bytes, max 107): %2")
                         .arg(QFile::encodeName(path).size())
                         .arg(path);
        return false;
    }
    const QString dir = QFileInfo(path).absolutePath();
    if (!paths::ensurePrivateDir(dir)) {
        if (error)
            *error = QStringLiteral("cannot create runtime directory %1").arg(dir);
        return false;
    }
    if (QFileInfo::exists(path)) {
        QLocalSocket probe;
        probe.connectToServer(path);
        if (probe.waitForConnected(300)) {
            if (error)
                *error = QStringLiteral("another omachatd is already running (%1)").arg(path);
            return false;
        }
        QLocalServer::removeServer(path); // stale socket from a crashed daemon
    }
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server.listen(path)) {
        if (error)
            *error = m_server.errorString();
        return false;
    }
    ::chmod(QFile::encodeName(path).constData(), 0600);
    m_path = path;
    return true;
}

void IpcServer::close()
{
    for (auto& c : m_clients) {
        c.socket->disconnect(this);
        c.socket->abort();
        c.socket->deleteLater();
    }
    m_clients.clear();
    if (m_server.isListening()) {
        m_server.close();
        QLocalServer::removeServer(m_path);
    }
}

IpcServer::Client* IpcServer::find(QLocalSocket* socket)
{
    for (auto& c : m_clients) {
        if (c.socket == socket)
            return &c;
    }
    return nullptr;
}

void IpcServer::onNewConnection()
{
    while (QLocalSocket* socket = m_server.nextPendingConnection()) {
        // Defense in depth on top of the 0600 socket: only our own uid.
        ucred cred{};
        socklen_t len = sizeof cred;
        if (::getsockopt(static_cast<int>(socket->socketDescriptor()), SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0
            || cred.uid != ::getuid()) {
            OMA_WARN("ipc", "rejected client from foreign uid", {"uid", static_cast<qint64>(cred.uid)});
            socket->abort();
            socket->deleteLater();
            continue;
        }
        m_clients.push_back(Client{socket, {}, false, {}});
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
            if (Client* c = find(socket))
                onReadyRead(*c);
        });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            m_clients.remove_if([socket](const Client& c) { return c.socket == socket; });
            emit clientDisconnected(socket);
            socket->deleteLater();
        });
        OMA_DEBUG("ipc", "client connected", {"clients", clientCount()});
    }
}

void IpcServer::onReadyRead(Client& c)
{
    QLocalSocket* socket = c.socket;
    c.decoder.feed(socket->readAll());
    QByteArray line;
    for (;;) {
        Client* current = find(socket);
        if (!current)
            return;
        const auto status = current->decoder.next(line);
        if (status == ipc::LineDecoder::Status::NeedMore)
            return;
        if (status == ipc::LineDecoder::Status::Oversized) {
            OMA_WARN("ipc", "client sent an oversized line; disconnecting");
            socket->abort();
            return;
        }
        const auto msg = ipc::parse(line);
        if (!msg) {
            send(socket, ipc::makeError(0, ipc::errors::BadRequest, QStringLiteral("malformed JSON")));
            continue;
        }
        const qint64 id = msg->value(QStringLiteral("id")).toInteger();
        const QString method = msg->value(QStringLiteral("method")).toString();
        const QJsonObject params = msg->value(QStringLiteral("params")).toObject();
        if (method.isEmpty()) {
            send(socket, ipc::makeError(id, ipc::errors::BadRequest, QStringLiteral("missing method")));
            continue;
        }
        if (method == u"events.subscribe") {
            current->subscribed = true;
            current->topics.clear();
            for (const auto& t : params.value(QStringLiteral("topics")).toArray())
                current->topics << t.toString();
            send(socket, ipc::makeResult(id, {{"subscribed", true}}));
            continue;
        }
        if (method == u"events.unsubscribe") {
            current->subscribed = false;
            send(socket, ipc::makeResult(id));
            continue;
        }
        if (m_handler)
            m_handler(method, params, Responder(this, socket, id));
        else
            send(socket, ipc::makeError(id, ipc::errors::Internal, QStringLiteral("daemon not ready")));
    }
}

void IpcServer::send(QLocalSocket* socket, const QJsonObject& msg)
{
    if (!socket || socket->state() != QLocalSocket::ConnectedState)
        return;
    if (socket->bytesToWrite() > kMaxBacklogBytes) {
        // A client that stopped reading must not grow our memory forever.
        OMA_WARN("ipc", "client is not reading; disconnecting");
        socket->abort();
        return;
    }
    socket->write(ipc::encode(msg));
}

void IpcServer::broadcast(const QString& event, const QJsonObject& data)
{
    const QJsonObject msg = ipc::makeEvent(event, data);
    QByteArray encoded;
    for (auto& c : m_clients) {
        if (!c.subscribed)
            continue;
        if (!c.topics.isEmpty()) {
            bool match = false;
            for (const auto& t : c.topics)
                match = match || event.startsWith(t);
            if (!match)
                continue;
        }
        if (encoded.isEmpty())
            encoded = ipc::encode(msg);
        if (c.socket->bytesToWrite() > kMaxBacklogBytes) {
            c.socket->abort();
            continue;
        }
        c.socket->write(encoded);
    }
}

} // namespace omachat::daemon
