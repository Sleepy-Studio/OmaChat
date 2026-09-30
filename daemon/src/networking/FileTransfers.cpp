#include "networking/FileTransfers.hpp"

#include "omachat/core/Log.hpp"
#include "omachat/ipc/IpcMessage.hpp"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonObject>
#include <QMimeDatabase>

namespace omachat::daemon {

namespace e = ipc::errors;

namespace {

// Progress events are for people; a few per second is plenty.
constexpr qint64 kProgressIntervalMs = 200;

QString formatMb(std::uint64_t bytes)
{
    return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', 0);
}

} // namespace

FileTransfers::FileTransfers(ServerConnection& conn, QObject* parent)
    : QObject(parent)
    , m_conn(conn)
{
    connect(&m_conn, &ServerConnection::synchronized, this, &FileTransfers::onReconnected);
    connect(&m_conn, &ServerConnection::resumed, this, &FileTransfers::onReconnected);
    connect(&m_conn, &ServerConnection::stateChanged, this, &FileTransfers::onConnectionState);
    m_waitTimer.setInterval(30 * 1000);
    connect(&m_waitTimer, &QTimer::timeout, this, &FileTransfers::expireWaiting);
}

FileTransfers::~FileTransfers()
{
    for (auto& [id, t] : m_transfers) {
        if (!t.upload && t.file)
            t.file->remove();
    }
}

FileTransfers::Transfer* FileTransfers::find(quint64 id)
{
    auto it = m_transfers.find(id);
    return it == m_transfers.end() ? nullptr : &it->second;
}

QJsonObject FileTransfers::json(const Transfer& t) const
{
    return {{"id", QString::number(t.id)}, {"direction", t.upload ? "upload" : "download"}, {"name", t.name},
        {"channel_id", idString(t.channelId)}, {"attachment_id", idString(t.attachmentId)},
        {"transferred", static_cast<double>(t.done)}, {"total", static_cast<double>(t.total)},
        {"waiting", t.waitingSinceMs != 0}};
}

void FileTransfers::send(quint64 id, proto::Envelope env, Reply onReply)
{
    const quint64 generation = m_conn.linkGeneration();
    m_conn.request(std::move(env), [this, id, generation, onReply = std::move(onReply)](const proto::Envelope& reply) {
        Transfer* t = find(id);
        if (!t || t->waitingSinceMs != 0)
            return;
        if (reply.has_error() && generation != m_conn.linkGeneration()) {
            // The connection went away underneath this request: wait for it.
            t->waitingSinceMs = QDateTime::currentMSecsSinceEpoch();
            t->inFlight = 0;
            m_waitTimer.start();
            OMA_INFO("transfer", "waiting for the connection", {"name", t->name});
            emitProgress(*t, true);
            return;
        }
        onReply(*t, reply);
    });
}

void FileTransfers::onReconnected()
{
    std::vector<quint64> waiting;
    for (const auto& [id, t] : m_transfers) {
        if (t.waitingSinceMs != 0)
            waiting.push_back(id);
    }
    for (quint64 id : waiting) {
        Transfer* t = find(id);
        if (!t)
            continue;
        t->waitingSinceMs = 0;
        OMA_INFO("transfer", "continuing after reconnect", {"name", t->name});
        if (!t->upload) {
            t->queued = t->done;
            if (t->total == 0) {
                t->inFlight = 1;
                proto::Envelope env;
                env.mutable_download()->set_attachment_id(t->attachmentId);
                env.mutable_download()->set_offset(0);
                send(id, std::move(env), [this, id](Transfer&, const proto::Envelope& r) { onDownloadChunk(id, r); });
            } else {
                pumpDownload(id);
            }
            continue;
        }
        if (t->attachmentId == 0 || !m_conn.capabilities().contains(QStringLiteral("attachments.resume"))) {
            beginUpload(id);
            continue;
        }
        proto::Envelope env;
        env.mutable_resume_upload()->set_attachment_id(t->attachmentId);
        send(id, std::move(env), [this, id](Transfer& live, const proto::Envelope& reply) {
            const std::uint64_t received = reply.has_error() ? 0 : reply.upload_ticket().received();
            if (reply.has_error() || received > live.total) {
                beginUpload(id); // the server lost it (restart, idle expiry): start over
                return;
            }
            // Re-read what the server already has so the checksum covers it.
            if (!live.file->isOpen() && !live.file->open(QIODevice::ReadOnly)) {
                fail(
                    id, e::StorageError, QStringLiteral("cannot read %1: %2").arg(live.name, live.file->errorString()));
                return;
            }
            live.file->seek(0);
            live.hash->reset();
            for (std::uint64_t left = received; left > 0;) {
                const QByteArray part = live.file->read(static_cast<qint64>(std::min<std::uint64_t>(left, 1 << 20)));
                if (part.isEmpty()) {
                    fail(id, e::StorageError, QStringLiteral("%1 changed while it was being uploaded").arg(live.name));
                    return;
                }
                live.hash->addData(part);
                left -= static_cast<std::uint64_t>(part.size());
            }
            live.chunk = std::max<std::uint32_t>(reply.upload_ticket().chunk_size(), 4096);
            live.queued = live.done = received;
            OMA_INFO("transfer", "upload resumed", {"name", live.name}, {"from", static_cast<qint64>(received)});
            emitProgress(live, true);
            if (received == live.total)
                finishUpload(id);
            else
                pumpUpload(id);
        });
    }
}

void FileTransfers::onConnectionState()
{
    // Logging out, switching account or a fatal error ends waiting transfers.
    const auto state = m_conn.state();
    if (state != ServerConnection::State::Disconnected && state != ServerConnection::State::NotConfigured
        && state != ServerConnection::State::Error)
        return;
    std::vector<quint64> waiting;
    for (const auto& [id, t] : m_transfers) {
        if (t.waitingSinceMs != 0)
            waiting.push_back(id);
    }
    for (quint64 id : waiting)
        fail(id, e::NetworkError, QStringLiteral("disconnected from the server"));
}

void FileTransfers::expireWaiting()
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    std::vector<quint64> expired;
    bool anyWaiting = false;
    for (const auto& [id, t] : m_transfers) {
        if (t.waitingSinceMs == 0)
            continue;
        anyWaiting = true;
        if (nowMs - t.waitingSinceMs > kMaxWaitMs)
            expired.push_back(id);
    }
    for (quint64 id : expired)
        fail(id, e::NetworkError, QStringLiteral("the connection did not come back in time"));
    if (!anyWaiting)
        m_waitTimer.stop();
}

QJsonArray FileTransfers::activeJson() const
{
    QJsonArray out;
    for (const auto& [id, t] : m_transfers)
        out.append(json(t));
    return out;
}

void FileTransfers::emitProgress(Transfer& t, bool force)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!force && nowMs - t.lastProgressMs < kProgressIntervalMs)
        return;
    t.lastProgressMs = nowMs;
    emit progress(json(t));
}

void FileTransfers::fail(quint64 id, const QString& code, const QString& message)
{
    auto it = m_transfers.find(id);
    if (it == m_transfers.end())
        return;
    Transfer t = std::move(it->second);
    m_transfers.erase(it);
    if (t.upload && t.attachmentId && m_conn.state() == ServerConnection::State::Connected) {
        // Best effort: the server usually dropped it already.
        proto::Envelope env;
        env.mutable_cancel_upload()->set_attachment_id(t.attachmentId);
        m_conn.request(std::move(env), [](const proto::Envelope&) { });
    }
    if (!t.upload && t.file) {
        t.file->close();
        t.file->remove();
    }
    QJsonObject data = json(t);
    data.insert(QStringLiteral("error"), QJsonObject{{"code", code}, {"message", message}});
    emit progress(data);
    OMA_INFO("transfer", "transfer failed", {"name", t.name}, {"code", code}, {"reason", message});
    if (t.callback)
        t.callback(Result{false, code, message, {}, {}});
}

void FileTransfers::succeed(quint64 id, Result result)
{
    auto it = m_transfers.find(id);
    if (it == m_transfers.end())
        return;
    Transfer t = std::move(it->second);
    m_transfers.erase(it);
    t.done = t.total;
    QJsonObject data = json(t);
    data.insert(QStringLiteral("complete"), true);
    emit progress(data);
    result.ok = true;
    if (t.callback)
        t.callback(result);
}

bool FileTransfers::cancel(quint64 transferId)
{
    if (!find(transferId))
        return false;
    fail(transferId, e::BadRequest, QStringLiteral("cancelled"));
    return true;
}

// ------------------------------------------------------------------ upload

quint64 FileTransfers::upload(quint64 channelId, const QString& path, Done done, bool channelArtwork)
{
    const quint64 id = m_nextId++;
    const QFileInfo info(path);
    auto fileError = [&](const QString& code, const QString& message) {
        if (done)
            done(Result{false, code, message, {}, {}});
        return quint64{0};
    };
    if (m_conn.maxUploadBytes() == 0)
        return fileError(e::BadRequest, QStringLiteral("this server does not accept attachments"));
    if (!info.isFile())
        return fileError(e::NotFound, QStringLiteral("no such file: %1").arg(path));
    if (info.size() == 0)
        return fileError(e::BadRequest, QStringLiteral("%1 is empty").arg(info.fileName()));
    if (static_cast<std::uint64_t>(info.size()) > m_conn.maxUploadBytes())
        return fileError(e::TooLarge,
            QStringLiteral("%1 is larger than this server's %2 MB limit")
                .arg(info.fileName(), formatMb(m_conn.maxUploadBytes())));

    Transfer t;
    t.id = id;
    t.upload = true;
    t.channelArtwork = channelArtwork;
    t.channelId = channelId;
    t.name = info.fileName();
    t.path = info.absoluteFilePath();
    t.file = std::make_unique<QFile>(t.path);
    if (!t.file->open(QIODevice::ReadOnly))
        return fileError(e::StorageError, QStringLiteral("cannot read %1: %2").arg(path, t.file->errorString()));
    t.total = static_cast<std::uint64_t>(t.file->size());
    t.hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    t.mimeType = QMimeDatabase().mimeTypeForFile(info).name();
    t.callback = std::move(done);
    m_transfers.emplace(id, std::move(t));
    beginUpload(id);
    return id;
}

void FileTransfers::beginUpload(quint64 id)
{
    Transfer* t = find(id);
    if (!t)
        return;
    // Also the restart path after a reconnect: everything from byte 0.
    if (!t->file->isOpen() && !t->file->open(QIODevice::ReadOnly)) {
        fail(id, e::StorageError, QStringLiteral("cannot read %1: %2").arg(t->name, t->file->errorString()));
        return;
    }
    t->file->seek(0);
    t->hash->reset();
    t->attachmentId = 0;
    t->queued = t->done = 0;
    t->inFlight = 0;
    proto::Envelope env;
    auto* b = env.mutable_begin_upload();
    b->set_channel_id(t->channelId);
    b->set_filename(t->name.toStdString());
    b->set_mime_type(t->mimeType.toStdString());
    b->set_size(t->total);
    b->set_channel_artwork(t->channelArtwork);
    send(id, std::move(env), [this, id](Transfer& live, const proto::Envelope& reply) {
        if (reply.has_error()) {
            fail(id, ipcErrorCode(reply.error().code()), QString::fromStdString(reply.error().message()));
            return;
        }
        live.attachmentId = reply.upload_ticket().attachment_id();
        live.chunk = std::max<std::uint32_t>(reply.upload_ticket().chunk_size(), 4096);
        emitProgress(live, true);
        pumpUpload(id);
    });
}

void FileTransfers::pumpUpload(quint64 id)
{
    Transfer* t = find(id);
    while (t && t->inFlight < kWindow && t->queued < t->total) {
        const QByteArray data
            = t->file->read(static_cast<qint64>(std::min<std::uint64_t>(t->chunk, t->total - t->queued)));
        if (data.isEmpty()) {
            fail(id, e::StorageError, QStringLiteral("cannot read %1: %2").arg(t->name, t->file->errorString()));
            return;
        }
        t->hash->addData(data);
        proto::Envelope env;
        auto* c = env.mutable_upload_chunk();
        c->set_attachment_id(t->attachmentId);
        c->set_offset(t->queued);
        c->set_data(data.toStdString());
        t->queued += static_cast<std::uint64_t>(data.size());
        t->inFlight += 1;
        const auto len = static_cast<std::uint64_t>(data.size());
        send(id, std::move(env), [this, id, len](Transfer& live, const proto::Envelope& reply) {
            if (reply.has_error()) {
                fail(id, ipcErrorCode(reply.error().code()), QString::fromStdString(reply.error().message()));
                return;
            }
            live.inFlight -= 1;
            live.done += len;
            if (live.done == live.total) {
                finishUpload(id);
                return;
            }
            emitProgress(live, false);
            pumpUpload(id);
        });
        t = find(id); // request() fails synchronously when offline, which ends the transfer
        if (t && t->waitingSinceMs != 0)
            return;
    }
}

void FileTransfers::finishUpload(quint64 id)
{
    Transfer* t = find(id);
    if (!t)
        return;
    // A file that changed size while we read it would be silently truncated.
    if (!t->file->atEnd() || static_cast<std::uint64_t>(QFileInfo(t->path).size()) != t->total) {
        fail(id, e::StorageError, QStringLiteral("%1 changed while it was being uploaded").arg(t->name));
        return;
    }
    proto::Envelope env;
    env.mutable_finish_upload()->set_attachment_id(t->attachmentId);
    env.mutable_finish_upload()->set_sha256(t->hash->result().toStdString());
    send(id, std::move(env), [this, id](Transfer&, const proto::Envelope& reply) {
        if (reply.has_error()) {
            fail(id, ipcErrorCode(reply.error().code()), QString::fromStdString(reply.error().message()));
            return;
        }
        Result r;
        r.attachment = reply.attachment();
        succeed(id, std::move(r));
    });
}

// ---------------------------------------------------------------- download

quint64 FileTransfers::download(quint64 attachmentId, const QString& destination, Done done)
{
    const quint64 id = m_nextId++;
    Transfer t;
    t.id = id;
    t.upload = false;
    t.attachmentId = attachmentId;
    t.path = destination;
    t.name = QFileInfo(destination).fileName();
    t.file = std::make_unique<QFile>(destination + QStringLiteral(".part"));
    if (!t.file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (done)
            done(Result{false, e::StorageError,
                QStringLiteral("cannot write %1: %2").arg(t.file->fileName(), t.file->errorString()), {}, {}});
        return 0;
    }
    t.callback = std::move(done);
    m_transfers.emplace(id, std::move(t));

    // The first reply reveals the size; the window opens after it.
    Transfer& ref = m_transfers.at(id);
    ref.inFlight = 1;
    proto::Envelope env;
    env.mutable_download()->set_attachment_id(attachmentId);
    env.mutable_download()->set_offset(0);
    send(id, std::move(env), [this, id](Transfer&, const proto::Envelope& reply) { onDownloadChunk(id, reply); });
    return id;
}

void FileTransfers::onDownloadChunk(quint64 id, const proto::Envelope& reply)
{
    Transfer* t = find(id);
    if (!t)
        return;
    if (reply.has_error()) {
        fail(id, ipcErrorCode(reply.error().code()), QString::fromStdString(reply.error().message()));
        return;
    }
    const auto& c = reply.file_chunk();
    t->inFlight -= 1;
    if (t->total == 0) {
        t->total = c.total_size();
        t->chunk = static_cast<std::uint32_t>(std::max<std::size_t>(c.data().size(), 4096));
        t->queued = c.data().size();
    }
    if (c.total_size() != t->total || c.offset() + c.data().size() > t->total || c.data().empty()) {
        fail(id, e::Internal, QStringLiteral("the server sent an inconsistent file chunk"));
        return;
    }
    if (!t->file->seek(static_cast<qint64>(c.offset()))
        || t->file->write(c.data().data(), static_cast<qint64>(c.data().size()))
            != static_cast<qint64>(c.data().size())) {
        fail(id, e::StorageError, QStringLiteral("cannot write %1: %2").arg(t->name, t->file->errorString()));
        return;
    }
    t->done += c.data().size();
    if (t->done >= t->total) {
        t->file->close();
        QFile::remove(t->path);
        if (!t->file->rename(t->path)) {
            fail(id, e::StorageError, QStringLiteral("cannot save %1: %2").arg(t->path, t->file->errorString()));
            return;
        }
        Result r;
        r.path = t->path;
        succeed(id, std::move(r));
        return;
    }
    emitProgress(*t, false);
    pumpDownload(id);
}

void FileTransfers::pumpDownload(quint64 id)
{
    Transfer* t = find(id);
    while (t && t->inFlight < kWindow && t->queued < t->total) {
        proto::Envelope env;
        env.mutable_download()->set_attachment_id(t->attachmentId);
        env.mutable_download()->set_offset(t->queued);
        env.mutable_download()->set_length(t->chunk);
        t->queued += std::min<std::uint64_t>(t->chunk, t->total - t->queued);
        t->inFlight += 1;
        send(id, std::move(env), [this, id](Transfer&, const proto::Envelope& reply) { onDownloadChunk(id, reply); });
        t = find(id); // request() fails synchronously when offline, which ends the transfer
        if (t && t->waitingSinceMs != 0)
            return;
    }
}

} // namespace omachat::daemon
