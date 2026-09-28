#pragma once

#include "omachat/ipc/IpcMessage.hpp"

#include <QHash>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QTimer>

#include <functional>

namespace omachat::ipc {

struct Reply {
    bool ok = false;
    QJsonObject result;
    QString errorCode;
    QString errorMessage;
};

// Asynchronous client for the omachatd socket, shared by the GUI and CLI.
class IpcClient : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(const Reply&)>;

    explicit IpcClient(QObject* parent = nullptr);
    ~IpcClient() override;

    void connectToDaemon(const QString& socketPath);
    void disconnectFromDaemon();
    bool isConnected() const { return m_socket.state() == QLocalSocket::ConnectedState; }

    // Sends a request. The callback always fires exactly once: with the
    // daemon's reply, or with a synthetic error on timeout/disconnect.
    void request(const QString& method, const QJsonObject& params, Callback callback, int timeoutMs = 20000);

signals:
    void connected();
    void disconnected();
    void connectionFailed(const QString& reason);
    void eventReceived(const QString& name, const QJsonObject& data);

private:
    void onReadyRead();
    void failAll(const QString& code, const QString& message);

    struct Pending {
        Callback callback;
        QTimer* timer = nullptr;
    };

    QLocalSocket m_socket;
    LineDecoder m_decoder;
    QHash<qint64, Pending> m_pending;
    qint64 m_nextId = 1;
};

} // namespace omachat::ipc
