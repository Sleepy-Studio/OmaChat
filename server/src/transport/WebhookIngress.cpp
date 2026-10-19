#include "transport/WebhookIngress.hpp"
#include <QMap>
#include <QTcpSocket>
#include <QTimer>
#include <memory>
namespace omachat::server {
WebhookIngress::WebhookIngress(QObject* parent)
    : QTcpServer(parent)
{
    setMaxPendingConnections(32);
    connect(this, &QTcpServer::newConnection, this, [this] {
        while (auto* socket = nextPendingConnection()) {
            if (m_active >= 32) {
                socket->abort();
                socket->deleteLater();
                continue;
            }
            ++m_active;
            socket->setReadBufferSize(24577);
            auto* timer = new QTimer(socket);
            timer->setSingleShot(true);
            timer->start(5000);
            connect(timer, &QTimer::timeout, socket, &QTcpSocket::abort);
            connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                --m_active;
                socket->deleteLater();
            });
            auto buffer = std::make_shared<QByteArray>();
            auto finished = std::make_shared<bool>(false);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer, finished] {
                if (*finished)
                    return;
                buffer->append(socket->readAll());
                auto respond = [&](int status) {
                    *finished = true;
                    const QByteArray body = QByteArray::number(status) + '\n';
                    socket->write("HTTP/1.1 " + QByteArray::number(status)
                        + " Result\r\nConnection: close\r\nContent-Type: text/plain\r\nCache-Control: "
                          "no-store\r\nContent-Length: "
                        + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    socket->disconnectFromHost();
                };
                if (buffer->size() > 24576) {
                    respond(413);
                    return;
                }
                const auto end = buffer->indexOf("\r\n\r\n");
                if (end < 0) {
                    if (buffer->size() > 8192)
                        respond(431);
                    return;
                }
                if (end > 8192) {
                    respond(431);
                    return;
                }
                auto lines = buffer->left(end).split('\n');
                const auto first = lines.takeFirst().trimmed().split(' ');
                if (first.size() != 3 || first[0] != "POST" || first[2] != "HTTP/1.1" || !first[1].startsWith("/hooks/")
                    || first[1].size() != 50) {
                    respond(400);
                    return;
                }
                const auto token = first[1].mid(7);
                for (const char ch : token)
                    if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-'
                            || ch == '_')) {
                        respond(400);
                        return;
                    }
                QMap<QByteArray, QByteArray> headers;
                for (auto line : lines) {
                    const auto colon = line.indexOf(':');
                    if (colon <= 0 || line.startsWith(' ') || line.startsWith('\t')) {
                        respond(400);
                        return;
                    }
                    const auto key = line.left(colon).toLower();
                    if (headers.contains(key)) {
                        respond(400);
                        return;
                    }
                    headers.insert(key, line.mid(colon + 1).trimmed());
                }
                if (!headers.contains("host") || headers.contains("transfer-encoding") || headers.contains("expect")) {
                    respond(400);
                    return;
                }
                bool ok = false;
                const auto lengthText = headers.value("content-length");
                for (char ch : lengthText)
                    if (ch < '0' || ch > '9') {
                        respond(400);
                        return;
                    }
                const auto length = lengthText.toLongLong(&ok);
                if (!ok || length <= 0) {
                    respond(400);
                    return;
                }
                if (length > 16384) {
                    respond(413);
                    return;
                }
                if (headers.value("content-type").toLower() != "application/json") {
                    respond(415);
                    return;
                }
                if (buffer->size() < end + 4 + length)
                    return;
                if (buffer->size() != end + 4 + length) {
                    respond(400);
                    return;
                }
                respond(handler ? handler(QString::fromLatin1(token), buffer->mid(end + 4)) : 503);
            });
        }
    });
}
} // namespace omachat::server
