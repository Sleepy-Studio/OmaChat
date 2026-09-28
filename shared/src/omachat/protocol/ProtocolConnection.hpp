#pragma once

#include "network.pb.h"
#include "omachat/protocol/Framing.hpp"

#include <QObject>
#include <QPointer>
#include <QSslSocket>

namespace omachat::protocol {

// Owns the framing and protobuf parsing for one TLS control connection.
// Used identically by omachat-server (accepted sockets) and omachatd
// (outgoing sockets).
class ProtocolConnection : public QObject {
    Q_OBJECT
public:
    // Takes ownership of `socket`.
    explicit ProtocolConnection(QSslSocket* socket, QObject* parent = nullptr);
    ~ProtocolConnection() override;

    QSslSocket* socket() const { return m_socket; }

    // Queues an envelope. Returns false (and aborts the connection) when the
    // peer is not draining its socket and the write backlog exceeds the cap.
    bool send(const proto::Envelope& envelope);

    void close();
    void abort();

    static constexpr qint64 kMaxWriteBacklog = 16 * 1024 * 1024;

signals:
    void envelopeReceived(const omachat::proto::Envelope& envelope);
    void protocolError(const QString& reason);
    void closed();

private:
    void onReadyRead();

    QPointer<QSslSocket> m_socket;
    FrameDecoder m_decoder;
    bool m_closed = false;
};

} // namespace omachat::protocol
