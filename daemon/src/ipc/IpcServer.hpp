#pragma once

#include "omachat/ipc/IpcMessage.hpp"

#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QPointer>
#include <QStringList>

#include <functional>
#include <list>

namespace omachat::daemon {

class IpcServer;

// Answers exactly one IPC request. Safe to keep and complete asynchronously;
// if the client disconnected in the meantime the reply is dropped.
class Responder {
public:
    Responder(IpcServer* server, QLocalSocket* socket, qint64 id);
    void ok(const QJsonObject& result = {}) const;
    void error(const QString& code, const QString& message) const;
    QLocalSocket* socket() const { return m_socket.data(); }

private:
    QPointer<IpcServer> m_server;
    QPointer<QLocalSocket> m_socket;
    qint64 m_id;
};

class IpcServer : public QObject {
    Q_OBJECT
public:
    using Handler = std::function<void(const QString& method, const QJsonObject& params, const Responder& r)>;

    explicit IpcServer(QObject* parent = nullptr);
    ~IpcServer() override;

    // Creates the runtime directory (0700), replaces a stale socket, refuses
    // to start when another daemon is already listening.
    bool listen(const QString& path, QString* error);
    void close();

    void setHandler(Handler h) { m_handler = std::move(h); }

    // Sends an event to every client subscribed to a matching topic.
    void broadcast(const QString& event, const QJsonObject& data);

    int clientCount() const { return static_cast<int>(m_clients.size()); }

    static constexpr qint64 kMaxBacklogBytes = 8 * 1024 * 1024;

signals:
    void clientDisconnected(QLocalSocket* socket);

private:
    friend class Responder;
    struct Client {
        QLocalSocket* socket = nullptr;
        ipc::LineDecoder decoder;
        bool subscribed = false;
        QStringList topics; // prefixes; empty = everything
    };

    void onNewConnection();
    void onReadyRead(Client& c);
    void send(QLocalSocket* socket, const QJsonObject& msg);
    Client* find(QLocalSocket* socket);

    QLocalServer m_server;
    QString m_path;
    std::list<Client> m_clients;
    Handler m_handler;
};

} // namespace omachat::daemon
