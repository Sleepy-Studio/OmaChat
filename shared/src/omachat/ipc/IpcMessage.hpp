#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <optional>

// Local IPC wire format: newline-delimited compact JSON objects over the
// per-user Unix socket ($XDG_RUNTIME_DIR/omachat/omachat.sock, mode 0600).
//
//   request : {"v":1,"id":7,"method":"voice.join","params":{...}}
//   response: {"v":1,"id":7,"ok":true,"result":{...}}
//             {"v":1,"id":7,"ok":false,"error":{"code":"PermissionDenied","message":"..."}}
//   event   : {"v":1,"event":"voice.state","data":{...}}
//
// JSON was chosen over protobuf here so that the Omarchy shell plugin
// (Quickshell QML) and shell scripts can speak the protocol without code
// generation. See docs/protocol.md#local-ipc.
namespace omachat::ipc {

inline constexpr qsizetype kMaxLineBytes = 1024 * 1024;

QByteArray encode(const QJsonObject& message);

QJsonObject makeRequest(qint64 id, const QString& method, const QJsonObject& params = {});
QJsonObject makeResult(qint64 id, const QJsonObject& result = {});
QJsonObject makeError(qint64 id, const QString& code, const QString& message);
QJsonObject makeEvent(const QString& name, const QJsonObject& data);

// Incremental newline splitter with a hard line limit.
class LineDecoder {
public:
    void feed(const QByteArray& chunk) { m_buffer.append(chunk); }

    enum class Status { Line, NeedMore, Oversized };
    Status next(QByteArray& line);

private:
    QByteArray m_buffer;
};

// Parses a line into an object; nullopt on malformed JSON or non-object.
std::optional<QJsonObject> parse(const QByteArray& line);

// Error codes shared by the daemon, CLI and GUI.
namespace errors {
inline const QString BadRequest = QStringLiteral("BadRequest");
inline const QString UnknownMethod = QStringLiteral("UnknownMethod");
inline const QString NotConnected = QStringLiteral("NotConnected");
inline const QString NetworkError = QStringLiteral("NetworkError");
inline const QString AuthenticationError = QStringLiteral("AuthenticationError");
inline const QString PermissionDenied = QStringLiteral("PermissionDenied");
inline const QString NotFound = QStringLiteral("NotFound");
inline const QString Conflict = QStringLiteral("Conflict");
inline const QString RateLimited = QStringLiteral("RateLimited");
inline const QString ProtocolMismatch = QStringLiteral("ProtocolMismatch");
inline const QString CertificateError = QStringLiteral("CertificateError");
inline const QString ServerUnavailable = QStringLiteral("ServerUnavailable");
inline const QString MediaDeviceUnavailable = QStringLiteral("MediaDeviceUnavailable");
inline const QString StorageError = QStringLiteral("StorageError");
inline const QString Timeout = QStringLiteral("Timeout");
inline const QString TooLarge = QStringLiteral("TooLarge");
inline const QString Internal = QStringLiteral("Internal");
} // namespace errors

} // namespace omachat::ipc
