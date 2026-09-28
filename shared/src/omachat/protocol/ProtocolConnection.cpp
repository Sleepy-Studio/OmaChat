#include "omachat/protocol/ProtocolConnection.hpp"

namespace omachat::protocol {

ProtocolConnection::ProtocolConnection(QSslSocket* socket, QObject* parent)
    : QObject(parent)
    , m_socket(socket)
{
    socket->setParent(this);
    connect(socket, &QSslSocket::readyRead, this, &ProtocolConnection::onReadyRead);
    connect(socket, &QSslSocket::disconnected, this, [this] {
        if (!m_closed) {
            m_closed = true;
            emit closed();
        }
    });
}

ProtocolConnection::~ProtocolConnection()
{
    m_closed = true;
    if (m_socket)
        m_socket->disconnect(this);
}

bool ProtocolConnection::send(const proto::Envelope& envelope)
{
    if (!m_socket || m_closed)
        return false;
    if (m_socket->bytesToWrite() > kMaxWriteBacklog) {
        emit protocolError(QStringLiteral("peer write backlog exceeded"));
        abort();
        return false;
    }
    std::string bytes;
    if (!envelope.SerializeToString(&bytes))
        return false;
    const QByteArray payload = QByteArray::fromStdString(bytes);
    m_socket->write(encodeFrame(payload));
    return true;
}

void ProtocolConnection::close()
{
    if (m_socket)
        m_socket->disconnectFromHost();
}

void ProtocolConnection::abort()
{
    if (m_socket)
        m_socket->abort();
    if (!m_closed) {
        m_closed = true;
        emit closed();
    }
}

void ProtocolConnection::onReadyRead()
{
    if (!m_socket)
        return;
    m_decoder.feed(m_socket->readAll());
    QByteArray frame;
    for (;;) {
        if (m_closed)
            return;
        const auto status = m_decoder.next(frame);
        if (status == FrameDecoder::Status::NeedMore)
            return;
        if (status == FrameDecoder::Status::Oversized) {
            emit protocolError(QStringLiteral("frame exceeds maximum size"));
            abort();
            return;
        }
        proto::Envelope envelope;
        if (!envelope.ParseFromArray(frame.constData(), static_cast<int>(frame.size()))) {
            emit protocolError(QStringLiteral("malformed envelope"));
            abort();
            return;
        }
        emit envelopeReceived(envelope);
    }
}

} // namespace omachat::protocol
