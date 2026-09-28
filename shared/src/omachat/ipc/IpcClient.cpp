#include "omachat/ipc/IpcClient.hpp"

namespace omachat::ipc {

IpcClient::IpcClient(QObject* parent)
    : QObject(parent)
{
    connect(&m_socket, &QLocalSocket::connected, this, &IpcClient::connected);
    connect(&m_socket, &QLocalSocket::disconnected, this, [this] {
        failAll(errors::NotConnected, QStringLiteral("daemon connection closed"));
        emit disconnected();
    });
    connect(&m_socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
        if (m_socket.state() != QLocalSocket::ConnectedState)
            emit connectionFailed(m_socket.errorString());
    });
    connect(&m_socket, &QLocalSocket::readyRead, this, &IpcClient::onReadyRead);
}

IpcClient::~IpcClient()
{
    // The socket member outlives m_pending during destruction and emits
    // disconnected() from its destructor; detach before that happens.
    m_socket.disconnect(this);
    m_socket.abort();
    for (auto& p : m_pending)
        delete p.timer;
}

void IpcClient::connectToDaemon(const QString& socketPath)
{
    m_socket.abort();
    m_decoder = LineDecoder{};
    m_socket.connectToServer(socketPath);
}

void IpcClient::disconnectFromDaemon()
{
    m_socket.disconnectFromServer();
}

void IpcClient::request(const QString& method, const QJsonObject& params, Callback callback, int timeoutMs)
{
    if (!isConnected()) {
        Reply r;
        r.errorCode = errors::NotConnected;
        r.errorMessage = QStringLiteral("omachatd is not running");
        if (callback)
            callback(r);
        return;
    }
    const qint64 id = m_nextId++;
    auto* timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, id] {
        auto it = m_pending.find(id);
        if (it == m_pending.end())
            return;
        Pending p = it.value();
        m_pending.erase(it);
        p.timer->deleteLater();
        Reply r;
        r.errorCode = errors::Timeout;
        r.errorMessage = QStringLiteral("daemon did not answer in time");
        if (p.callback)
            p.callback(r);
    });
    timer->start(timeoutMs);
    m_pending.insert(id, Pending{std::move(callback), timer});
    m_socket.write(encode(makeRequest(id, method, params)));
}

void IpcClient::onReadyRead()
{
    m_decoder.feed(m_socket.readAll());
    QByteArray line;
    for (;;) {
        const auto status = m_decoder.next(line);
        if (status == LineDecoder::Status::NeedMore)
            return;
        if (status == LineDecoder::Status::Oversized) {
            m_socket.abort();
            return;
        }
        const auto msg = parse(line);
        if (!msg)
            continue;
        if (msg->contains(QStringLiteral("event"))) {
            emit eventReceived(
                msg->value(QStringLiteral("event")).toString(), msg->value(QStringLiteral("data")).toObject());
            continue;
        }
        const qint64 id = msg->value(QStringLiteral("id")).toInteger();
        auto it = m_pending.find(id);
        if (it == m_pending.end())
            continue;
        Pending p = it.value();
        m_pending.erase(it);
        p.timer->stop();
        p.timer->deleteLater();
        Reply r;
        r.ok = msg->value(QStringLiteral("ok")).toBool();
        r.result = msg->value(QStringLiteral("result")).toObject();
        const QJsonObject err = msg->value(QStringLiteral("error")).toObject();
        r.errorCode = err.value(QStringLiteral("code")).toString();
        r.errorMessage = err.value(QStringLiteral("message")).toString();
        if (p.callback)
            p.callback(r);
    }
}

void IpcClient::failAll(const QString& code, const QString& message)
{
    auto pending = std::exchange(m_pending, {});
    for (auto& p : pending) {
        p.timer->stop();
        p.timer->deleteLater();
        Reply r;
        r.errorCode = code;
        r.errorMessage = message;
        if (p.callback)
            p.callback(r);
    }
}

} // namespace omachat::ipc
