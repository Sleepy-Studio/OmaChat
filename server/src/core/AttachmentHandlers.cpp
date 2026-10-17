#include "core/ArtworkImages.hpp"
#include "core/ChatServer.hpp"
#include "omachat/core/Log.hpp"
#include "omachat/core/Validation.hpp"
#include <QCoreApplication>
#include <QFileInfo>
#include <QPointer>
#include <QSaveFile>
#include <QThreadPool>

#include <QBuffer>
#include <QDir>
#include <QImageReader>
#include <QImageWriter>

#include <algorithm>

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
    if (m.has_artwork_framing() && (!m.channel_artwork() || !artwork::validFraming(m.artwork_framing()))) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("invalid artwork framing"));
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
    if (m.has_artwork_framing()) {
        u.framing = m.artwork_framing();
        u.framing.mutable_unknown_fields()->clear();
    }
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
    if (it == m_uploads.end() || it->second.connId != s.connId || it->second.userId != s.userId) {
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
    if (u->preparing) {
        replyError(s, rid, proto::ERROR_RATE_LIMITED, QStringLiteral("this artwork is being prepared"), 1000);
        return;
    }
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
    if (u->file->isOpen() && !u->file->flush()) {
        abortUpload(id);
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("the server could not store this file"));
        return;
    }
    u->file->close();

    if (!u->channelArtwork) {
        finishPreparedUpload(s, rid, id, digest, {});
        return;
    }
    if (m_artworkJobs >= 2) {
        replyError(s, rid, proto::ERROR_RATE_LIMITED, QStringLiteral("artwork workers are busy; retry shortly"), 1000);
        return;
    }
    QFile source(u->file->fileName());
    if (!source.open(QIODevice::ReadOnly)) {
        abortUpload(id);
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("cannot read artwork upload"));
        return;
    }
    const QByteArray bytes = source.read(2 * 1024 * 1024 + 1);
    source.close();
    u->preparing = true;
    ++m_artworkJobs;
    const QPointer<ChatServer> guard(this);
    const auto connId = s.connId;
    const auto userId = s.userId;
    const auto framing = u->framing;
    QThreadPool::globalInstance()->start([guard, connId, userId, rid, id, digest, bytes, framing] {
        const auto images = artwork::prepare(bytes, framing.kind() == proto::ArtworkFraming::NONE ? nullptr : &framing);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [guard, connId, userId, rid, id, digest, images] {
                if (!guard)
                    return;
                --guard->m_artworkJobs;
                auto it = guard->m_uploads.find(id);
                if (it == guard->m_uploads.end())
                    return;
                it->second.preparing = false;
                auto* session = guard->sessionFor(connId);
                if (!session || session->userId != userId || it->second.connId != connId)
                    return; // The uploader can resume and request finish again.
                if (!guard->m_state.can(it->second.channelId, userId, ViewChannel | ManageChannel)) {
                    guard->abortUpload(id);
                    guard->replyError(*session, rid, proto::ERROR_PERMISSION_DENIED,
                        QStringLiteral("artwork management access changed during preparation"));
                    return;
                }
                guard->finishPreparedUpload(*session, rid, id, digest, images);
            },
            Qt::QueuedConnection);
    });
}

void ChatServer::finishPreparedUpload(
    Session& s, std::uint64_t rid, Id id, const QByteArray& digest, const artwork::Images& images)
{
    Upload* u = uploadFor(s, rid, id);
    if (!u)
        return;
    QByteArray normalized;
    QByteArray imageFormat;
    const bool framed = u->framing.kind() != proto::ArtworkFraming::NONE;
    if (u->channelArtwork) {
        if (!images.error.isEmpty()) {
            abortUpload(id);
            replyError(s, rid, proto::ERROR_BAD_REQUEST, images.error);
            return;
        }
        normalized = images.full;
        imageFormat = images.format;
        QFile clean(u->file->fileName());
        if (!clean.open(QIODevice::WriteOnly | QIODevice::Truncate) || clean.write(normalized) != normalized.size()
            || !clean.flush()) {
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
    if (framed)
        a.artworkFraming = QByteArray::fromStdString(u->framing.SerializeAsString());
    const auto saveVariant = [&](const QString& suffix, const QByteArray& bytes) {
        QSaveFile file(attachmentPath(id) + suffix);
        return file.open(QIODevice::WriteOnly) && file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
            && file.write(bytes) == bytes.size() && file.commit();
    };
    if (framed
        && (!saveVariant(QStringLiteral(".source"), images.source)
            || !saveVariant(QStringLiteral(".small"), images.small))) {
        removeAttachmentFiles({id});
        abortUpload(id);
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not store artwork variants"));
        return;
    }
    const QString partPath = u->file->fileName();
    if (!QFile::rename(partPath, attachmentPath(id)) || !m_store.insertAttachment(a)) {
        removeAttachmentFiles({id});
        abortUpload(id);
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("the server could not store this file"));
        return;
    }
    const auto completedFraming = u->framing;
    m_uploads.erase(id);

    proto::Envelope env;
    auto* p = env.mutable_attachment();
    p->set_id(a.id);
    p->set_filename(a.filename.toStdString());
    p->set_mime_type(a.mimeType.toStdString());
    p->set_size(a.size);
    p->set_sha256(a.sha256.toStdString());
    if (framed)
        *p->mutable_artwork_framing() = completedFraming;
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
    const auto references = a ? m_store.artworkChannels(a->id) : std::nullopt;
    if (!references) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("attachment not found"));
        return;
    }
    const auto& artworkChannels = *references;
    const Id artworkServer = artworkChannels.empty() && a ? m_store.artworkServer(a->id) : 0;
    const bool channelArtworkVisible
        = std::ranges::any_of(artworkChannels, [&](Id channel) { return m_state.can(channel, s.userId, ViewChannel); });
    const bool serverVisible
        = artworkServer && m_state.server(artworkServer) && m_state.server(artworkServer)->members.contains(s.userId);
    const bool visible = a
        && (!artworkChannels.empty() ? channelArtworkVisible
                : artworkServer      ? serverVisible
                : a->messageId == 0  ? a->uploaderId == s.userId
                                     : m_state.can(a->channelId, s.userId, ViewChannel | ReadHistory));
    if (!visible) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("attachment not found"));
        return;
    }
    if (!proto::DownloadRequest_ArtworkVariant_IsValid(m.artwork_variant())
        || (m.artwork_variant() != proto::DownloadRequest::FULL && !a->artwork)) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("invalid artwork variant"));
        return;
    }
    proto::ArtworkFraming framing;
    if (!a->artworkFraming.isEmpty()
        && (!framing.ParseFromString(a->artworkFraming.toStdString()) || !artwork::validFraming(framing))) {
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("artwork framing is unreadable"));
        return;
    }
    framing.mutable_unknown_fields()->clear();
    QString path = attachmentPath(a->id);
    if (m.artwork_variant() == proto::DownloadRequest::SOURCE) {
        const bool manager = !artworkChannels.empty()
            ? std::ranges::any_of(artworkChannels,
                  [&](Id channel) { return m_state.can(channel, s.userId, ViewChannel | ManageChannel); })
            : artworkServer
            ? m_state.canInServer(artworkServer, s.userId, ManageServer)
            : a->uploaderId == s.userId && m_state.can(a->channelId, s.userId, ViewChannel | ManageChannel);
        if (!manager || a->artworkFraming.isEmpty()) {
            replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("artwork source not found"));
            return;
        }
        path += QStringLiteral(".source");
    } else if (m.artwork_variant() == proto::DownloadRequest::SMALL && !a->artworkFraming.isEmpty()) {
        path += QStringLiteral(".small");
    }
    const auto fileSize = QFileInfo(path).size();
    if (fileSize <= 0 || (a->artwork && fileSize > 2 * 1024 * 1024)) {
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("artwork variant is missing on the server"));
        return;
    }
    const auto total = static_cast<std::uint64_t>(fileSize);
    if (m.offset() > total) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("offset beyond end of file"));
        return;
    }
    const std::uint64_t want = m.length() == 0 ? kChunkBytes : std::min<std::uint64_t>(m.length(), kChunkBytes);
    const std::uint64_t len = std::min<std::uint64_t>(want, total - m.offset());
    QFile file(path);
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
    c->set_total_size(total);
    if (!a->artworkFraming.isEmpty())
        *c->mutable_artwork_framing() = framing;
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
    for (Id id : ids) {
        QFile::remove(attachmentPath(id));
        QFile::remove(attachmentPath(id) + QStringLiteral(".small"));
        QFile::remove(attachmentPath(id) + QStringLiteral(".source"));
    }
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
        } else if (name.endsWith(u".small") || name.endsWith(u".source")) {
            const Id variantId = name.left(name.indexOf(u'.')).toULongLong(&isNumber);
            if (isNumber && !keep.contains(variantId))
                QFile::remove(dir.filePath(name));
        } else if (name.contains(u".source.") || name.contains(u".small.")) {
            name.left(name.indexOf(u'.')).toULongLong(&isNumber);
            if (isNumber)
                QFile::remove(dir.filePath(name)); // Interrupted atomic variant writes.
        } else if (name.endsWith(u".part")) {
            const Id partId = name.chopped(5).toULongLong(&isNumber);
            if (isNumber && !m_uploads.contains(partId))
                QFile::remove(dir.filePath(name));
        }
    }
}

} // namespace omachat::server
