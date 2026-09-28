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
        {"transferred", static_cast<double>(t.done)}, {"total", static_cast<double>(t.total)}};
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
        m_conn.request(std::move(env), [](const proto::Envelope&) {});
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

quint64 FileTransfers::upload(quint64 channelId, const QString& path, Done done)
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
    t.channelId = channelId;
    t.name = info.fileName();
    t.path = info.absoluteFilePath();
    t.file = std::make_unique<QFile>(t.path);
    if (!t.file->open(QIODevice::ReadOnly))
        return fileError(e::StorageError, QStringLiteral("cannot read %1: %2").arg(path, t.file->errorString()));
    t.total = static_cast<std::uint64_t>(t.file->size());
    t.hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    t.callback = std::move(done);
    m_transfers.emplace(id, std::move(t));

    proto::Envelope env;
    auto* b = env.mutable_begin_upload();
    b->set_channel_id(channelId);
    b->set_filename(info.fileName().toStdString());
    b->set_mime_type(QMimeDatabase().mimeTypeForFile(info).name().toStdString());
    b->set_size(static_cast<std::uint64_t>(info.size()));
    m_conn.request(std::move(env), [this, id](const proto::Envelope& reply) {
        Transfer* live = find(id);
        if (!live)
            return;
        if (reply.has_error()) {
            fail(id, ipcErrorCode(reply.error().code()), QString::fromStdString(reply.error().message()));
            return;
        }
        live->attachmentId = reply.upload_ticket().attachment_id();
        live->chunk = std::max<std::uint32_t>(reply.upload_ticket().chunk_size(), 4096);
        emitProgress(*live, true);
        pumpUpload(id);
    });
    return id;
}

void FileTransfers::pumpUpload(quint64 id)
{
    Transfer* t = find(id);
    while (t && t->inFlight < kWindow && t->queued < t->total) {
        const QByteArray data = t->file->read(static_cast<qint64>(std::min<std::uint64_t>(t->chunk, t->total - t->queued)));
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
        m_conn.request(std::move(env), [this, id, len](const proto::Envelope& reply) {
            Transfer* live = find(id);
            if (!live)
                return;
            if (reply.has_error()) {
                fail(id, ipcErrorCode(reply.error().code()), QString::fromStdString(reply.error().message()));
                return;
            }
            live->inFlight -= 1;
            live->done += len;
            if (live->done == live->total) {
                finishUpload(id);
                return;
            }
            emitProgress(*live, false);
            pumpUpload(id);
        });
        t = find(id); // request() fails synchronously when offline, which ends the transfer
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
    t->file->close();
    proto::Envelope env;
    env.mutable_finish_upload()->set_attachment_id(t->attachmentId);
    env.mutable_finish_upload()->set_sha256(t->hash->result().toStdString());
    m_conn.request(std::move(env), [this, id](const proto::Envelope& reply) {
        if (!find(id))
            return;
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
    m_conn.request(std::move(env), [this, id](const proto::Envelope& reply) { onDownloadChunk(id, reply); });
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
        m_conn.request(std::move(env), [this, id](const proto::Envelope& reply) { onDownloadChunk(id, reply); });
        t = find(id); // request() fails synchronously when offline, which ends the transfer
    }
}

} // namespace omachat::daemon
