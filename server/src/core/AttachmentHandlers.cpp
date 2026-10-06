#include "core/ChatServer.hpp"
#include "omachat/core/Log.hpp"
#include "omachat/core/Validation.hpp"

#include <QBuffer>
#include <QDir>
#include <QImageReader>
#include <QImageWriter>

namespace omachat::server {

using namespace omachat::permissions;

namespace {

// Big enough to move files quickly, small enough that a chunk never holds a
// chat message back for long on the shared control connection.
constexpr std::uint32_t kChunkBytes = 512 * 1024;
constexpr int kMaxPendingPerUser = 10;
constexpr std::int64_t kPendingLifetimeMs = 60 * 60 * 1000;
constexpr std::int64_t kIdleUploadMs = 10 * 60 * 1000;

} // namespace

QString ChatServer::attachmentPath(Id id) const
{
    return QDir(m_config.filesPath).filePath(QString::number(id));
}

std::uint64_t ChatServer::maxUploadBytes() const
{
    return static_cast<std::uint64_t>(m_config.maxUploadMb) * 1024u * 1024u;
}

void ChatServer::handleBeginUpload(Session& s, std::uint64_t rid, const proto::BeginUploadRequest& m)
{
    if (!limit(s, rid, s.uploads))
        return;
    const ChannelRecord* c = m_state.channel(m.channel_id());
    if (!c || !m_state.can(c->id, s.userId, ViewChannel)) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("channel not found"));
        return;
    }
    if (m.channel_artwork()) {
        if (!c->serverId || !m_state.can(c->id, s.userId, ManageChannel)) {
            replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot manage artwork here"));
            return;
        }
    } else if (c->kind != ChannelKind::Text && c->kind != ChannelKind::Dm && c->kind != ChannelKind::GroupDm) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("this channel does not accept messages"));
        return;
    }
    if (!m.channel_artwork() && !m_state.can(c->id, s.userId, SendMessages | AttachFiles)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot attach files here"));
        return;
    }
    const auto name = validation::filename(QString::fromStdString(m.filename()));
    if (!name) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("filenames are 1-200 characters"));
        return;
    }
    if (m.size() == 0) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("empty files cannot be attached"));
        return;
    }
    if (m.size() > maxUploadBytes() || (m.channel_artwork() && m.size() > 2 * 1024 * 1024)) {
        replyError(s, rid, proto::ERROR_TOO_LARGE,
            QStringLiteral("files on this server are limited to %1 MB").arg(m_config.maxUploadMb));
        return;
    }
    int pending = m_store.pendingAttachmentCount(s.userId);
    for (const auto& [id, u] : m_uploads)
        pending += u.userId == s.userId ? 1 : 0;
    if (pending >= kMaxPendingPerUser) {
        replyError(s, rid, proto::ERROR_RATE_LIMITED, QStringLiteral("too many unsent attachments"), 5000);
        return;
    }

    Upload u;
    u.id = m_ids.next();
    u.connId = s.connId;
    u.userId = s.userId;
    u.channelId = c->id;
    u.channelArtwork = m.channel_artwork();
    u.filename = *name;
    u.mimeType = validation::mimeType(QString::fromStdString(m.mime_type()));
    u.size = m.size();
    u.lastActivity = now();
    u.hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    u.file = std::make_unique<QFile>(attachmentPath(u.id) + QStringLiteral(".part"));
    if (!u.file->open(QIODevice::WriteOnly | QIODevice::NewOnly)
        || !u.file->setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        OMA_ERROR("files", "cannot create upload file", {"path", u.file->fileName()}, {"error", u.file->errorString()});
        u.file->remove();
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("the server cannot store files right now"));
        return;
    }
    const Id id = u.id;
    m_uploads.emplace(id, std::move(u));

    proto::Envelope env;
    auto* t = env.mutable_upload_ticket();
    t->set_attachment_id(id);
    t->set_chunk_size(kChunkBytes);
    reply(s, rid, std::move(env));
}

ChatServer::Upload* ChatServer::uploadFor(Session& s, std::uint64_t rid, Id attachmentId)
{
    auto it = m_uploads.find(attachmentId);
    // Another connection's upload looks exactly like a missing one.
    if (it == m_uploads.end() || it->second.connId != s.connId) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("no such upload"));
        return nullptr;
    }
    return &it->second;
}

void ChatServer::handleResumeUpload(Session& s, std::uint64_t rid, const proto::ResumeUploadRequest& m)
{
    auto it = m_uploads.find(m.attachment_id());
    // Only its uploader may pick an upload up again, from any connection.
    if (it == m_uploads.end() || it->second.userId != s.userId) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("no such upload"));
        return;
    }
    Upload& u = it->second;
    u.connId = s.connId;
    u.lastActivity = now();
    proto::Envelope env;
    auto* t = env.mutable_upload_ticket();
    t->set_attachment_id(u.id);
    t->set_chunk_size(kChunkBytes);
    t->set_received(u.received);
    reply(s, rid, std::move(env));
}

void ChatServer::handleUploadChunk(Session& s, std::uint64_t rid, const proto::UploadChunkRequest& m)
{
    if (!limit(s, rid, s.transfer))
        return;
    Upload* u = uploadFor(s, rid, m.attachment_id());
    if (!u)
        return;
    const auto len = static_cast<std::uint64_t>(m.data().size());
    if (m.offset() != u->received || len == 0 || len > kChunkBytes || u->received + len > u->size) {
        // The upload cannot recover from a gap or overrun; drop it so the
        // client starts over instead of storing a corrupt file.
        abortUpload(u->id);
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("chunk out of sequence; upload cancelled"));
        return;
    }
    if (u->file->write(m.data().data(), static_cast<qint64>(len)) != static_cast<qint64>(len)) {
        OMA_ERROR("files", "upload write failed", {"path", u->file->fileName()}, {"error", u->file->errorString()});
        abortUpload(u->id);
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("the server could not store this file"));
        return;
    }
    u->hash->addData(QByteArrayView(m.data().data(), static_cast<qsizetype>(len)));
    u->received += len;
    u->lastActivity = now();
    replyOk(s, rid);
}

void ChatServer::handleFinishUpload(Session& s, std::uint64_t rid, const proto::FinishUploadRequest& m)
{
    Upload* u = uploadFor(s, rid, m.attachment_id());
    if (!u)
        return;
    const Id id = u->id;
    if (u->received != u->size) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST,
            QStringLiteral("upload incomplete: %1 of %2 bytes").arg(u->received).arg(u->size));
        return;
    }
    const QByteArray digest = u->hash->result();
    if (!m.sha256().empty() && QByteArray::fromStdString(m.sha256()) != digest) {
        abortUpload(id);
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("checksum mismatch; upload cancelled"));
        return;
    }
    if (!u->file->flush()) {
        abortUpload(id);
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("the server could not store this file"));
        return;
    }
    u->file->close();

    QByteArray normalized;
    QByteArray imageFormat;
    if (u->channelArtwork) {
        QImageReader reader(u->file->fileName());
        reader.setDecideFormatFromContent(true);
        const QByteArray sourceFormat = reader.format().toLower();
        const QSize dimensions = reader.size();
        if ((sourceFormat != "png" && sourceFormat != "jpeg" && sourceFormat != "webp") || !dimensions.isValid()
            || dimensions.width() > 6000 || dimensions.height() > 6000
            || qint64(dimensions.width()) * dimensions.height() > 12000000 || reader.imageCount() > 1) {
            abortUpload(id);
            replyError(s, rid, proto::ERROR_BAD_REQUEST,
                QStringLiteral("channel artwork must be a static PNG, JPEG, or WebP image"));
            return;
        }
        QImage image = reader.read();
        if (image.isNull()) {
            abortUpload(id);
            replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("could not decode channel artwork"));
            return;
        }
        if (image.width() > 1600 || image.height() > 1600)
            image = image.scaled(1600, 1600, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        imageFormat = image.hasAlphaChannel() ? "PNG" : "JPEG";
        QBuffer buffer(&normalized);
        buffer.open(QIODevice::WriteOnly);
        QImageWriter writer(&buffer, imageFormat);
        writer.setQuality(85);
        if (!writer.write(image) || normalized.isEmpty() || normalized.size() > 2 * 1024 * 1024) {
            abortUpload(id);
            replyError(
                s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("channel artwork could not fit the 2 MB image limit"));
            return;
        }
        QFile clean(u->file->fileName());
        if (!clean.open(QIODevice::WriteOnly | QIODevice::Truncate) || clean.write(normalized) != normalized.size()) {
            abortUpload(id);
            replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not save channel artwork"));
            return;
        }
        clean.close();
    }

    AttachmentRecord a;
    a.id = id;
    a.channelId = u->channelId;
    a.uploaderId = u->userId;
    a.filename = u->filename;
    a.mimeType = u->mimeType;
    a.size = u->size;
    a.sha256 = digest;
    if (u->channelArtwork) {
        a.size = static_cast<std::uint64_t>(normalized.size());
        a.sha256 = QCryptographicHash::hash(normalized, QCryptographicHash::Sha256);
        a.mimeType = imageFormat == "PNG" ? QStringLiteral("image/png") : QStringLiteral("image/jpeg");
        a.filename
            = imageFormat == "PNG" ? QStringLiteral("channel-artwork.png") : QStringLiteral("channel-artwork.jpg");
    }
    a.createdAt = now();
    a.artwork = u->channelArtwork;
    const QString partPath = u->file->fileName();
    if (!QFile::rename(partPath, attachmentPath(id)) || !m_store.insertAttachment(a)) {
        QFile::remove(attachmentPath(id));
        abortUpload(id);
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("the server could not store this file"));
        return;
    }
    m_uploads.erase(id);

    proto::Envelope env;
    auto* p = env.mutable_attachment();
    p->set_id(a.id);
    p->set_filename(a.filename.toStdString());
    p->set_mime_type(a.mimeType.toStdString());
    p->set_size(a.size);
    p->set_sha256(a.sha256.toStdString());
    reply(s, rid, std::move(env));
}

void ChatServer::handleCancelUpload(Session& s, std::uint64_t rid, const proto::CancelUploadRequest& m)
{
    if (auto it = m_uploads.find(m.attachment_id()); it != m_uploads.end()) {
        // Its uploader may cancel it from any connection (e.g. after a reconnect).
        if (it->second.userId != s.userId) {
            replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("no such upload"));
            return;
        }
        abortUpload(m.attachment_id());
        replyOk(s, rid);
        return;
    }
    // A finished but unsent attachment can be withdrawn by its uploader.
    const auto a = m_store.attachment(m.attachment_id());
    if (!a || a->uploaderId != s.userId || a->messageId != 0 || m_store.artworkChannel(a->id)
        || m_store.artworkServer(a->id)) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("no such upload"));
        return;
    }
    m_store.deleteAttachment(a->id);
    removeAttachmentFiles({a->id});
    replyOk(s, rid);
}

void ChatServer::handleDownload(Session& s, std::uint64_t rid, const proto::DownloadRequest& m)
{
    if (!limit(s, rid, s.transfer))
        return;
    const auto a = m_store.attachment(m.attachment_id());
    const Id artworkChannel = a ? m_store.artworkChannel(a->id) : 0;
    const Id artworkServer = artworkChannel ? 0 : (a ? m_store.artworkServer(a->id) : 0);
    const bool serverVisible
        = artworkServer && m_state.server(artworkServer) && m_state.server(artworkServer)->members.contains(s.userId);
    const bool visible = a
        && (artworkChannel          ? m_state.can(artworkChannel, s.userId, ViewChannel)
                : artworkServer     ? serverVisible
                : a->messageId == 0 ? a->uploaderId == s.userId
                                    : m_state.can(a->channelId, s.userId, ViewChannel | ReadHistory));
    if (!visible) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("attachment not found"));
        return;
    }
    if (m.offset() > a->size) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("offset beyond end of file"));
        return;
    }
    const std::uint64_t want = m.length() == 0 ? kChunkBytes : std::min<std::uint64_t>(m.length(), kChunkBytes);
    const std::uint64_t len = std::min<std::uint64_t>(want, a->size - m.offset());
    QFile file(attachmentPath(a->id));
    QByteArray data;
    if (!file.open(QIODevice::ReadOnly) || !file.seek(static_cast<qint64>(m.offset()))
        || (data = file.read(static_cast<qint64>(len))).size() != static_cast<qsizetype>(len)) {
        OMA_ERROR("files", "attachment unreadable", {"id", QString::number(a->id)}, {"error", file.errorString()});
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("this file is missing on the server"));
        return;
    }
    proto::Envelope env;
    auto* c = env.mutable_file_chunk();
    c->set_attachment_id(a->id);
    c->set_offset(m.offset());
    c->set_data(data.toStdString());
    c->set_total_size(a->size);
    reply(s, rid, std::move(env));
}

void ChatServer::abortUpload(Id uploadId)
{
    auto it = m_uploads.find(uploadId);
    if (it == m_uploads.end())
        return;
    it->second.file->close();
    it->second.file->remove();
    m_uploads.erase(it);
}

void ChatServer::detachUploadsOf(quint64 connId)
{
    // Kept for ResumeUpload until the idle sweep removes them.
    for (auto& [id, u] : m_uploads) {
        if (u.connId == connId)
            u.connId = 0;
    }
}

void ChatServer::removeAttachmentFiles(const std::vector<Id>& ids)
{
    for (Id id : ids)
        QFile::remove(attachmentPath(id));
}

void ChatServer::collectAttachmentGarbage()
{
    const auto t = now();
    removeAttachmentFiles(m_store.purgePendingAttachments(t - kPendingLifetimeMs));

    std::vector<Id> idle;
    for (const auto& [id, u] : m_uploads) {
        if (t - u.lastActivity > kIdleUploadMs)
            idle.push_back(id);
    }
    for (Id id : idle)
        abortUpload(id);

    // Deleting a channel or server cascades attachment rows in SQL; sweep the
    // files those rows left behind, plus .part files from a previous run.
    const auto known = m_store.allAttachmentIds();
    const std::set<Id> keep(known.begin(), known.end());
    const QDir dir(m_config.filesPath);
    for (const QString& name : dir.entryList(QDir::Files)) {
        bool isNumber = false;
        const Id id = name.toULongLong(&isNumber);
        if (isNumber && !keep.contains(id)) {
            QFile::remove(dir.filePath(name));
        } else if (name.endsWith(u".part")) {
            const Id partId = name.chopped(5).toULongLong(&isNumber);
            if (isNumber && !m_uploads.contains(partId))
                QFile::remove(dir.filePath(name));
        }
    }
}

} // namespace omachat::server
