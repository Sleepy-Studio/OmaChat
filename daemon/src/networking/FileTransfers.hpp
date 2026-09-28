#pragma once

#include "network.pb.h"
#include "networking/ServerConnection.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QObject>
#include <QTimer>

#include <functional>
#include <map>
#include <memory>

namespace omachat::daemon {

// Moves attachment bytes over the control connection in pipelined chunks.
// Transfers are owned by the daemon, not by the IPC client that asked for
// them, so closing the window does not cancel an upload in flight. When the
// connection drops they wait and continue after the reconnect: downloads
// from the bytes already written, uploads from what the server kept
// (ResumeUpload), or from the start on servers without that.
class FileTransfers : public QObject {
    Q_OBJECT
public:
    struct Result {
        bool ok = false;
        QString code; // IPC error code when !ok
        QString message;
        proto::Attachment attachment; // uploads
        QString path; // downloads
    };
    using Done = std::function<void(const Result&)>;

    explicit FileTransfers(ServerConnection& conn, QObject* parent = nullptr);
    ~FileTransfers() override;

    // Uploads a local file as a pending attachment in `channelId`. Returns
    // the transfer id used in progress events.
    quint64 upload(quint64 channelId, const QString& path, Done done);
    // Downloads an attachment to `destination`, writing `<destination>.part`
    // first so a partial file never looks complete.
    quint64 download(quint64 attachmentId, const QString& destination, Done done);
    bool cancel(quint64 transferId);
    QJsonArray activeJson() const;

    // Chunks kept in flight per transfer: enough to hide one round trip.
    static constexpr int kWindow = 4;
    // A transfer waiting for the connection longer than this fails. Just
    // under the server's 10 minute idle limit for interrupted uploads.
    static constexpr qint64 kMaxWaitMs = 9 * 60 * 1000;

signals:
    void progress(const QJsonObject& data);

private:
    struct Transfer {
        quint64 id = 0;
        bool upload = true;
        quint64 channelId = 0;
        quint64 attachmentId = 0;
        QString name;
        QString path;
        std::unique_ptr<QFile> file;
        std::unique_ptr<QCryptographicHash> hash;
        std::uint64_t total = 0;
        std::uint64_t queued = 0; // bytes requested (download) or sent (upload)
        std::uint64_t done = 0; // bytes acknowledged / written
        std::uint32_t chunk = 0;
        int inFlight = 0;
        qint64 lastProgressMs = 0;
        qint64 waitingSinceMs = 0; // 0 = not waiting for the connection
        QString mimeType;
        Done callback;
    };

    using Reply = std::function<void(Transfer&, const proto::Envelope&)>;
    // Sends a request for transfer `id`. The callback only runs for a live
    // transfer and a real answer; a lost connection makes it wait instead.
    void send(quint64 id, proto::Envelope env, Reply onReply);
    void beginUpload(quint64 id);
    void onReconnected();
    void onConnectionState();
    void expireWaiting();
    void pumpUpload(quint64 id);
    void finishUpload(quint64 id);
    void pumpDownload(quint64 id);
    void onDownloadChunk(quint64 id, const proto::Envelope& reply);
    void fail(quint64 id, const QString& code, const QString& message);
    void succeed(quint64 id, Result result);
    void emitProgress(Transfer& t, bool force);
    Transfer* find(quint64 id);
    QJsonObject json(const Transfer& t) const;

    ServerConnection& m_conn;
    QTimer m_waitTimer;
    std::map<quint64, Transfer> m_transfers;
    quint64 m_nextId = 1;
};

} // namespace omachat::daemon
