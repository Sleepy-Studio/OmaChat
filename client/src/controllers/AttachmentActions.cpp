// Composer attachments, upload progress, image previews and saving files.
// The daemon does all transfers; this file only keeps the UI's view of them.

#include "controllers/AppController.hpp"

#include "omachat/core/Paths.hpp"
#include "platform/ArtworkCrop.hpp"
#include "text/PasteContent.hpp"

#include <QClipboard>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QMimeDatabase>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QThreadPool>
#include <QUrl>
#include <atomic>
#include <memory>

namespace omachat::client {

namespace {

QString cropIdentityFingerprint(const QImage& source, const QString& identity, quint64 generation)
{
    return QString::fromLatin1(QCryptographicHash::hash(
        (artwork::fingerprint(source) + QLatin1Char('/') + identity + QLatin1Char('/') + QString::number(generation))
            .toUtf8(),
        QCryptographicHash::Sha256)
            .toHex());
}

constexpr int kMaxFilesPerMessage = 10;
std::atomic<int> artworkJobs{0};
bool reserveArtworkJob()
{
    if (artworkJobs.fetch_add(1) < 2)
        return true;
    artworkJobs.fetch_sub(1);
    return false;
}
// Previews download automatically, so keep them to what a glance needs.
constexpr double kMaxPreviewBytes = 10.0 * 1024 * 1024;

QString localPath(const QVariant& v)
{
    QUrl url = v.toUrl();
    if (!url.isValid() || url.scheme().isEmpty())
        url = QUrl::fromUserInput(v.toString());
    return url.isLocalFile() ? url.toLocalFile() : QString();
}

// Pasted images are written here so the daemon can upload them like any
// other file. They must outlive the upload, so they are only pruned by age.
QString pastedDir()
{
    return paths::cacheDir() + QStringLiteral("/pasted");
}

// Handing a received file to the desktop is only done for types whose
// default handlers are viewers; anything else (scripts, .desktop entries,
// archives) is saved instead so opening it is a deliberate user action.
bool safeToOpen(const QString& path)
{
    const QMimeType type = QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchContent);
    const QString name = type.name();
    return name.startsWith(u"image/") || name.startsWith(u"video/") || name.startsWith(u"audio/")
        || name == u"application/pdf" || name == u"text/plain";
}

} // namespace

QString AppController::formatSize(double bytes) const
{
    if (bytes < 1024)
        return tr("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return tr("%1 KB").arg(bytes / 1024, 0, 'f', 1);
    return tr("%1 MB").arg(bytes / (1024 * 1024), 0, 'f', 1);
}

QString AppController::formatDuration(qint64 ms) const
{
    if (ms < 0)
        ms = 0;
    const qint64 totalSeconds = ms / 1000;
    const qint64 minutes = totalSeconds / 60;
    const qint64 seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

QVariantList AppController::uploads() const
{
    QVariantList out;
    for (const auto& t : m_uploads) {
        if (t.value(QStringLiteral("account")).toString() == accountId()
            && t.value(QStringLiteral("channel_id")).toString() == m_selectedChannel)
            out.append(t.toVariantMap());
    }
    return out;
}

void AppController::addFiles(const QVariantList& urls)
{
    if (!attachmentsSupported()) {
        showNotice(tr("This server does not accept attachments."), true);
        return;
    }
    const double limit = maxUploadBytes();
    for (const auto& v : urls) {
        const QString path = localPath(v);
        const QFileInfo info(path);
        if (path.isEmpty() || !info.isFile()) {
            showNotice(tr("Only local files can be attached."), true);
            continue;
        }
        if (info.size() == 0) {
            showNotice(tr("%1 is empty.").arg(info.fileName()), true);
            continue;
        }
        if (static_cast<double>(info.size()) > limit) {
            showNotice(tr("%1 is larger than this server's %2 limit.").arg(info.fileName(), formatSize(limit)), true);
            continue;
        }
        const bool duplicate = std::ranges::any_of(m_pendingFiles,
            [&](const QVariant& f) { return f.toMap().value(QStringLiteral("path")) == info.absoluteFilePath(); });
        if (duplicate)
            continue;
        if (m_pendingFiles.size() >= kMaxFilesPerMessage) {
            showNotice(tr("A message can carry at most %1 files.").arg(kMaxFilesPerMessage), true);
            break;
        }
        m_pendingFiles.append(QVariantMap{
            {"path", info.absoluteFilePath()}, {"name", info.fileName()}, {"size", static_cast<double>(info.size())}});
    }
    emit attachmentsChanged();
    emit focusComposer();
}

bool AppController::pasteAttachment()
{
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    if (!mime)
        return false;
    switch (PasteContent::classify(*mime)) {
    case PasteContent::Kind::Text:
        return false;
    case PasteContent::Kind::Files: {
        QVariantList urls;
        for (const QUrl& u : PasteContent::localFiles(*mime))
            urls.append(u);
        addFiles(urls);
        return true;
    }
    case PasteContent::Kind::Image:
        break;
    }
    if (!attachmentsSupported()) {
        showNotice(tr("This server does not accept attachments."), true);
        return true;
    }
    const QString path = paths::ensurePrivateDir(paths::cacheDir()) && paths::ensurePrivateDir(pastedDir())
        ? PasteContent::saveImage(*mime, pastedDir())
        : QString();
    if (path.isEmpty()) {
        showNotice(tr("Could not read the image on the clipboard."), true);
        return true;
    }
    addFiles({QUrl::fromLocalFile(path)});
    return true;
}

void AppController::prunePastedImages()
{
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-7);
    for (const QFileInfo& entry : QDir(pastedDir()).entryInfoList(QDir::Files)) {
        if (entry.lastModified() < cutoff)
            QFile::remove(entry.absoluteFilePath());
    }
}

void AppController::removePendingFile(int index)
{
    if (index < 0 || index >= m_pendingFiles.size())
        return;
    m_pendingFiles.removeAt(index);
    emit attachmentsChanged();
}

void AppController::refreshTransfers()
{
    const QString account = accountId();
    if (account.isEmpty())
        return;
    // Only reconcile rows known before this request; newer progress events win.
    QHash<QString, QJsonObject> before;
    for (auto it = m_uploads.cbegin(); it != m_uploads.cend(); ++it)
        if (it->value(QStringLiteral("account")).toString() == account)
            before.insert(it.key(), it.value());
    m_link.request(QStringLiteral("transfer.list"), {}, [this, account, before](const ipc::Reply& r) {
        if (!r.ok || account != accountId())
            return;
        QSet<QString> active;
        for (const auto& value : r.result.value(QStringLiteral("transfers")).toArray()) {
            auto data = value.toObject();
            const QString key = account + u':' + data.value(QStringLiteral("id")).toString();
            active.insert(key);
            if (!m_uploads.contains(key) || m_uploads.value(key) == before.value(key)) {
                data.insert(QStringLiteral("account"), account);
                onTransferProgress(data);
            }
        }
        for (auto it = before.cbegin(); it != before.cend(); ++it) {
            if (!active.contains(it.key()) && m_uploads.value(it.key()) == it.value()
                && !it->contains(QStringLiteral("error")) && !it->value(QStringLiteral("complete")).toBool()) {
                auto row = it.value();
                row.insert(QStringLiteral("error"),
                    QJsonObject{
                        {"message", tr("Transfer is no longer active. Check message delivery before sending again.")}});
                m_uploads.insert(it.key(), row);
            }
        }
        emit attachmentsChanged();
    });
}

void AppController::onTransferProgress(const QJsonObject& data)
{
    if (data.value(QStringLiteral("direction")).toString() != u"upload")
        return;
    const QString id = data.value(QStringLiteral("id")).toString();
    const QString account = data.value(QStringLiteral("account")).toString(accountId());
    const QString key = account + u':' + id;
    QJsonObject row = data;
    row.insert(QStringLiteral("account"), account);
    row.insert(QStringLiteral("key"), key);
    m_uploads.insert(key, row);
    // Keep outcomes until dismissed, bounded independently of active transfers.
    if (m_uploads.size() > 100) {
        for (auto it = m_uploads.begin(); it != m_uploads.end(); ++it) {
            if (it.key() != key
                && (it->contains(QStringLiteral("error")) || it->value(QStringLiteral("complete")).toBool())) {
                m_uploads.erase(it);
                break;
            }
        }
    }
    emit attachmentsChanged();
}

void AppController::dismissTransfer(const QString& key)
{
    const auto it = m_uploads.constFind(key);
    if (it == m_uploads.cend() || it->value(QStringLiteral("account")).toString() != accountId()
        || (!it->contains(QStringLiteral("error")) && !it->value(QStringLiteral("complete")).toBool()))
        return;
    m_uploads.remove(key);
    emit attachmentsChanged();
}

void AppController::cancelTransfer(const QString& transferId)
{
    call(QStringLiteral("transfer.cancel"), {{"id", transferId}});
}

void AppController::refreshArtworkCache(bool modelReady)
{
    const QJsonObject currentAccount = account();
    const QString identity = accountId().isEmpty() || selfId().isEmpty()
        ? QString()
        : QString::fromUtf8(QJsonDocument(
              QJsonArray{accountId(), currentAccount.value(QStringLiteral("host")),
                  currentAccount.value(QStringLiteral("port")), currentAccount.value(QStringLiteral("username")),
                  currentAccount.value(QStringLiteral("trusted_fingerprint")), selfId()})
                  .toJson(QJsonDocument::Compact));
    if (modelReady)
        m_artworkModelIdentity = identity;
    const bool accessLost = state() == u"login_required" || state() == u"not_configured"
        || m_status.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString()
            == ipc::errors::AuthenticationError;
    if (accessLost)
        m_artworkModelIdentity.clear();
    QSet<QString> authorizedIds;
    QSet<QString> active;
    auto collect = [](const QJsonObject& object, QSet<QString>& into) {
        for (const auto* key : {"icon_attachment_id", "banner_attachment_id"}) {
            const QString id = object.value(QLatin1StringView(key)).toString();
            if (!id.isEmpty() && id != u"0")
                into.insert(id);
        }
    };
    if (!identity.isEmpty() && identity == m_artworkModelIdentity) {
        for (const auto& server : m_serversById)
            collect(server, authorizedIds);
        for (const auto& ch : m_channelsById)
            collect(ch, authorizedIds);
        // The rail and channel list show icons as well as selected banners.
        for (const auto& server : m_serversById) {
            const QString icon = server.value(QStringLiteral("icon_attachment_id")).toString();
            if (!icon.isEmpty())
                active.insert(icon);
        }
        collect(m_serversById.value(m_selectedServer), active);
        for (const auto& ch : m_channelsById)
            if (ch.value(QStringLiteral("server_id")).toString() == m_selectedServer) {
                const QString icon = ch.value(QStringLiteral("icon_attachment_id")).toString();
                if (!icon.isEmpty())
                    active.insert(icon);
            }
        collect(m_channelsById.value(m_selectedChannel), active);
    }
    const quint64 before = m_artworkCache.generation();
    const QSet<QString> removed
        = m_artworkCache.reconcile(identity, authorizedIds, active, accessLost || identity == m_artworkModelIdentity);
    bool changed = false;
    for (const auto& id : removed) {
        changed |= m_previews.remove(id) > 0;
        changed |= m_previewErrors.remove(id) > 0;
        m_previewRequests.remove(id);
    }
    if (before != m_artworkCache.generation()) {
        // Attachment IDs have meaning only within an authenticated endpoint.
        changed |= !m_previews.isEmpty() || !m_previewErrors.isEmpty();
        m_previews.clear();
        m_previewErrors.clear();
        m_previewRequests.clear();
        m_videoThumbnails.clear();
        m_videoThumbnailRequests.clear();
        m_pendingMediaRequests.clear();
        m_pendingAudioOpens.clear();
        m_pendingVideoOpens.clear();
        emit videoThumbnailsChanged();
        emit artworkGenerationChanged();
    }
    if (changed)
        emit previewsChanged();
}

void AppController::requestPreview(const QString& attachmentId, const QString& filename, double size)
{
    refreshArtworkCache();
    bool artwork = m_artworkCache.authorized(attachmentId);
    const auto identifiesArtwork = [&](const QJsonObject& object) {
        return object.value(QStringLiteral("icon_attachment_id")).toString() == attachmentId
            || object.value(QStringLiteral("banner_attachment_id")).toString() == attachmentId;
    };
    for (const auto& object : m_serversById)
        artwork |= identifiesArtwork(object);
    for (const auto& object : m_channelsById)
        artwork |= identifiesArtwork(object);
    if (artwork && !m_artworkCache.authorized(attachmentId))
        return;
    const QString cachedArtwork = artwork ? m_artworkCache.lookup(attachmentId) : QString();
    if (artwork && m_previews.contains(attachmentId)) {
        // Detect deleted/corrupt disk entries before considering a URL reusable.
        if (cachedArtwork.isEmpty()) {
            m_previews.remove(attachmentId);
            emit previewsChanged();
        }
    }
    if (size > kMaxPreviewBytes || m_previews.contains(attachmentId) || m_previewRequests.contains(attachmentId))
        return;
    if (artwork) {
        if (!cachedArtwork.isEmpty()) {
            m_previewErrors.remove(attachmentId);
            m_previews.insert(attachmentId, QUrl::fromLocalFile(cachedArtwork));
            emit previewsChanged();
            return;
        }
    }
    if (artwork && m_artworkCache.stagingFull()) {
        // Keep one pending timer per authorized ID while transfer slots are full.
        // Capacity pressure is loading, not a download failure.
        m_previewRequests.insert(attachmentId);
        if (m_previewErrors.remove(attachmentId))
            emit previewsChanged();
        const quint64 waitingGeneration = m_artworkCache.generation();
        QTimer::singleShot(250, this, [this, attachmentId, filename, size, waitingGeneration] {
            if (waitingGeneration != m_artworkCache.generation())
                return;
            m_previewRequests.remove(attachmentId);
            if (m_artworkCache.authorized(attachmentId))
                requestPreview(attachmentId, filename, size);
        });
        return;
    }
    const QString staging = artwork ? m_artworkCache.stagingPath(attachmentId) : QString();
    if (artwork && staging.isEmpty()) {
        m_previewErrors.insert(attachmentId, true);
        emit previewsChanged();
        return;
    }
    m_previewRequests.insert(attachmentId);
    if (m_previewErrors.remove(attachmentId))
        emit previewsChanged();
    const quint64 generation = m_artworkCache.generation();
    m_link.request(
        QStringLiteral("attachment.download"),
        {{"attachment", attachmentId}, {"filename", filename}, {"to", artwork ? staging : QStringLiteral("cache")},
            {"size", size}},
        [this, attachmentId, artwork, staging, generation](const ipc::Reply& r) {
            if (generation != m_artworkCache.generation() || (artwork && !m_artworkCache.authorized(attachmentId))) {
                m_artworkCache.discard(staging);
                return;
            }
            m_previewRequests.remove(attachmentId);
            QString path;
            if (r.ok)
                path = artwork ? m_artworkCache.store(attachmentId, staging, generation)
                               : r.result.value(QStringLiteral("path")).toString();
            else
                m_artworkCache.discard(staging);
            if (path.isEmpty()) {
                m_previewErrors.insert(attachmentId, true);
                emit previewsChanged();
                return;
            }
            m_previews.insert(attachmentId, QUrl::fromLocalFile(path));
            emit previewsChanged();
        },
        0);
}

// Unlike requestPreview, this is only ever called from a deliberate tap on a
// media attachment's play button, so it skips the eager-preview size cap.
void AppController::requestMedia(const QString& attachmentId, const QString& filename)
{
    if (m_previews.contains(attachmentId) || m_previewRequests.contains(attachmentId))
        return;
    if (m_videoThumbnailRequests.contains(attachmentId)) {
        m_pendingMediaRequests.insert(attachmentId);
        return;
    }
    m_previewRequests.insert(attachmentId);
    const quint64 generation = m_artworkCache.generation();
    m_link.request(
        QStringLiteral("attachment.download"), {{"attachment", attachmentId}, {"filename", filename}, {"to", "cache"}},
        [this, attachmentId, filename, generation](const ipc::Reply& r) {
            if (generation != m_artworkCache.generation())
                return;
            m_previewRequests.remove(attachmentId);
            if (!r.ok) {
                if (m_pendingAudioOpens.remove(attachmentId))
                    showNotice(tr("Cannot open %1: %2").arg(filename, r.errorMessage), true);
                if (m_pendingVideoOpens.remove(attachmentId))
                    openVideoAttachment(attachmentId, filename);
                return;
            }
            m_previews.insert(attachmentId, QUrl::fromLocalFile(r.result.value(QStringLiteral("path")).toString()));
            emit previewsChanged();
            if (m_pendingAudioOpens.remove(attachmentId))
                openAudioAttachment(attachmentId, filename);
            if (m_pendingVideoOpens.remove(attachmentId))
                openVideoAttachment(attachmentId, filename);
        },
        0);
}

void AppController::requestVideoThumbnail(const QString& attachmentId, const QString& filename, double size)
{
    if (m_videoThumbnails.contains(attachmentId) || m_videoThumbnailRequests.contains(attachmentId))
        return;
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    const QString directory = paths::cacheDir() + QStringLiteral("/thumbnails");
    if (ffmpeg.isEmpty() || !paths::ensurePrivateDir(paths::cacheDir()) || !paths::ensurePrivateDir(directory)) {
        m_videoThumbnails.insert(attachmentId, false);
        emit videoThumbnailsChanged();
        return;
    }

    const QString key = QString::fromLatin1(
        QCryptographicHash::hash((m_artworkModelIdentity + u':' + attachmentId).toUtf8(), QCryptographicHash::Sha256)
            .toHex());
    const QString thumbnail = directory + u'/' + key + QStringLiteral(".jpg");
    if (QFileInfo(thumbnail).isFile()) {
        m_videoThumbnails.insert(attachmentId, QUrl::fromLocalFile(thumbnail));
        emit videoThumbnailsChanged();
        return;
    }

    m_videoThumbnailRequests.insert(attachmentId);
    const quint64 generation = m_artworkCache.generation();
    m_link.request(
        QStringLiteral("attachment.download"),
        {{"attachment", attachmentId}, {"filename", filename}, {"to", "cache"}, {"size", size}},
        [this, attachmentId, filename, ffmpeg, thumbnail, generation](const ipc::Reply& r) {
            if (generation != m_artworkCache.generation())
                return;
            if (!r.ok) {
                m_videoThumbnailRequests.remove(attachmentId);
                m_videoThumbnails.insert(attachmentId, false);
                emit videoThumbnailsChanged();
                if (m_pendingMediaRequests.remove(attachmentId))
                    requestMedia(attachmentId, filename);
                if (m_pendingVideoOpens.remove(attachmentId))
                    openVideoAttachment(attachmentId, filename);
                return;
            }
            const QString path = r.result.value(QStringLiteral("path")).toString();
            if (!QFileInfo(path).isFile()) {
                m_videoThumbnailRequests.remove(attachmentId);
                m_videoThumbnails.insert(attachmentId, false);
                emit videoThumbnailsChanged();
                if (m_pendingMediaRequests.remove(attachmentId))
                    requestMedia(attachmentId, filename);
                if (m_pendingVideoOpens.remove(attachmentId))
                    openVideoAttachment(attachmentId, filename);
                return;
            }
            m_previews.insert(attachmentId, QUrl::fromLocalFile(path));
            emit previewsChanged();
            m_pendingMediaRequests.remove(attachmentId);
            if (m_pendingVideoOpens.remove(attachmentId))
                openVideoAttachment(attachmentId, filename);
            auto* process = new QProcess(this);
            connect(process, &QProcess::finished, this,
                [this, process, attachmentId, thumbnail, generation](int exitCode, QProcess::ExitStatus status) {
                    if (generation != m_artworkCache.generation()) {
                        process->deleteLater();
                        return;
                    }
                    m_videoThumbnailRequests.remove(attachmentId);
                    if (status == QProcess::NormalExit && exitCode == 0 && QFileInfo(thumbnail).size() > 0) {
                        m_videoThumbnails.insert(attachmentId, QUrl::fromLocalFile(thumbnail));
                        emit videoThumbnailsChanged();
                    } else {
                        QFile::remove(thumbnail);
                        m_videoThumbnails.insert(attachmentId, false);
                        emit videoThumbnailsChanged();
                    }
                    process->deleteLater();
                });
            connect(process, &QProcess::errorOccurred, this,
                [this, process, attachmentId, generation](QProcess::ProcessError error) {
                    if (generation != m_artworkCache.generation()) {
                        process->deleteLater();
                        return;
                    }
                    if (error == QProcess::FailedToStart) {
                        m_videoThumbnailRequests.remove(attachmentId);
                        m_videoThumbnails.insert(attachmentId, false);
                        emit videoThumbnailsChanged();
                        process->deleteLater();
                    }
                });
            process->start(ffmpeg,
                {QStringLiteral("-nostdin"), QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-threads"),
                    QStringLiteral("1"), QStringLiteral("-i"), path, QStringLiteral("-frames:v"), QStringLiteral("1"),
                    QStringLiteral("-vf"), QStringLiteral("scale=640:-2"), QStringLiteral("-y"), thumbnail});
            QTimer::singleShot(15000, process, [process] {
                if (process->state() != QProcess::NotRunning)
                    process->kill();
            });
        },
        0);
}

void AppController::saveAttachment(const QString& attachmentId, const QString& filename)
{
    showNotice(tr("Downloading %1…").arg(filename));
    m_link.request(
        QStringLiteral("attachment.download"), {{"attachment", attachmentId}, {"filename", filename}},
        [this](const ipc::Reply& r) {
            if (!r.ok) {
                showNotice(
                    tr("Download failed: %1").arg(r.errorMessage.isEmpty() ? r.errorCode : r.errorMessage), true);
                return;
            }
            const QString path = r.result.value(QStringLiteral("path")).toString();
            showNotice(tr("Saved to %1").arg(QDir::toNativeSeparators(path)));
        },
        0);
}

void AppController::openAttachment(const QString& attachmentId, const QString& filename, double size)
{
    const quint64 generation = m_artworkCache.generation();
    m_link.request(
        QStringLiteral("attachment.download"),
        {{"attachment", attachmentId}, {"filename", filename}, {"to", "cache"}, {"size", size}},
        [this, attachmentId, filename, generation](const ipc::Reply& r) {
            if (generation != m_artworkCache.generation())
                return;
            if (!r.ok) {
                showNotice(tr("Cannot open %1: %2").arg(filename, r.errorMessage), true);
                return;
            }
            const QString path = r.result.value(QStringLiteral("path")).toString();
            if (safeToOpen(path)) {
                if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
                    showNotice(tr("No application could open %1.").arg(filename), true);
            } else {
                saveAttachment(attachmentId, filename);
            }
        },
        0);
}

void AppController::openAudioAttachment(const QString& attachmentId, const QString& filename)
{
    const QString cached = m_previews.value(attachmentId).toUrl().toLocalFile();
    if (QFileInfo(cached).isFile()) {
        if (safeToOpen(cached)) {
            if (!QDesktopServices::openUrl(QUrl::fromLocalFile(cached)))
                showNotice(tr("No application could open %1.").arg(filename), true);
        } else {
            saveAttachment(attachmentId, filename);
        }
        return;
    }
    m_pendingAudioOpens.insert(attachmentId);
    requestMedia(attachmentId, filename);
}

void AppController::openVideoAttachment(const QString& attachmentId, const QString& filename)
{
    const QString mpv = QStandardPaths::findExecutable(QStringLiteral("mpv"));
    if (mpv.isEmpty()) {
        showNotice(tr("Install MPV to open video attachments."), true);
        return;
    }
    const QString cached = m_previews.value(attachmentId).toUrl().toLocalFile();
    if (QFileInfo(cached).isFile()) {
        if (!QProcess::startDetached(mpv, {QStringLiteral("--"), cached}))
            showNotice(tr("Could not start MPV for %1.").arg(filename), true);
        return;
    }
    if (m_videoThumbnailRequests.contains(attachmentId) || m_previewRequests.contains(attachmentId)) {
        m_pendingVideoOpens.insert(attachmentId);
        return;
    }
    const quint64 generation = m_artworkCache.generation();
    m_link.request(
        QStringLiteral("attachment.download"), {{"attachment", attachmentId}, {"filename", filename}, {"to", "cache"}},
        [this, mpv, filename, generation](const ipc::Reply& r) {
            if (generation != m_artworkCache.generation())
                return;
            if (!r.ok) {
                showNotice(tr("Cannot open %1: %2").arg(filename, r.errorMessage), true);
                return;
            }
            const QString path = r.result.value(QStringLiteral("path")).toString();
            if (!QFileInfo(path).isFile() || !QProcess::startDetached(mpv, {QStringLiteral("--"), path}))
                showNotice(tr("Could not start MPV for %1.").arg(filename), true);
        },
        0);
}

void AppController::prepareArtworkCrop(int requestId, const QUrl& fileUrl, const QString& kind)
{
    if (!fileUrl.isLocalFile() || (kind != u"icon" && kind != u"banner")) {
        emit artworkCropPrepared(requestId, {}, tr("Choose a local PNG, JPEG or WebP image."), {}, 0, 0);
        return;
    }
    if (!reserveArtworkJob()) {
        emit artworkCropPrepared(requestId, {}, tr("Another image is being prepared. Try again shortly."), {}, 0, 0);
        return;
    }
    const QPointer<AppController> guard(this);
    const QString identity = accountId();
    const quint64 generation = m_artworkCache.generation();
    QThreadPool::globalInstance()->start([guard, requestId, identity, generation, path = fileUrl.toLocalFile()] {
        QString error;
        const QImage source = artwork::readSource(path, &error);
        const QString preview = source.isNull() ? QString() : artwork::previewUrl(source);
        const QString fingerprint = source.isNull() ? QString() : cropIdentityFingerprint(source, identity, generation);
        const QSize size = source.size();
        artworkJobs.fetch_sub(1);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [guard, requestId, preview, error, fingerprint, size, identity, generation] {
                if (!guard)
                    return;
                if (guard->accountId() != identity || guard->m_artworkCache.generation() != generation) {
                    emit guard->artworkCropPrepared(
                        requestId, {}, tr("Account or artwork access changed. Choose the image again."), {}, 0, 0);
                    return;
                }
                emit guard->artworkCropPrepared(requestId, preview, error, fingerprint, size.width(), size.height());
            },
            Qt::QueuedConnection);
    });
}

void AppController::setChannelArtworkCrop(const QString& id, const QString& kind, const QUrl& fileUrl, double focalX,
    double focalY, double zoom, const QString& fingerprint)
{
    const quint64 operation = ++m_cropOperation;
    if (!fileUrl.isLocalFile()) {
        emit administrationFinished(QStringLiteral("channel.artwork.crop"), id, tr("Choose a local image file."));
        return;
    }
    if (!reserveArtworkJob()) {
        emit administrationFinished(
            QStringLiteral("channel.artwork.crop"), id, tr("Another image is being prepared. Try again shortly."));
        return;
    }
    const QPointer<AppController> guard(this);
    const QString identity = accountId();
    const QString server = m_selectedServer;
    const quint64 generation = m_artworkCache.generation();
    QThreadPool::globalInstance()->start([guard, id, kind, path = fileUrl.toLocalFile(), focalX, focalY, zoom, identity,
                                             generation, fingerprint, server, operation] {
        QString error;
        const QImage source = artwork::readSource(path, &error);
        const bool matches = !source.isNull() && !fingerprint.isEmpty()
            && cropIdentityFingerprint(source, identity, generation) == fingerprint;
        if (!source.isNull() && !matches)
            error = QStringLiteral("The selected image changed. Choose it again before uploading.");
        const QByteArray bytes
            = !matches ? QByteArray() : artwork::encodeCrop(source, kind, focalX, focalY, zoom, &error);
        artworkJobs.fetch_sub(1);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [guard, id, kind, bytes, error, identity, generation, server, operation] {
                if (!guard)
                    return;
                // The originating dialog cancels on context/generation changes.
                // Suppress its result so it cannot settle a subsequently opened crop.
                if (guard->m_cropOperation != operation || guard->accountId() != identity
                    || guard->m_artworkCache.generation() != generation || guard->m_selectedServer != server)
                    return;
                if (!error.isEmpty()) {
                    emit guard->administrationFinished(QStringLiteral("channel.artwork.crop"), id, error);
                    return;
                }
                auto file = std::make_shared<QTemporaryFile>(
                    QDir::tempPath() + QStringLiteral("/omachat-artwork-XXXXXX.png"));
                if (!file->open() || file->write(bytes) != bytes.size() || !file->flush()) {
                    emit guard->administrationFinished(QStringLiteral("channel.artwork.crop"), id,
                        tr("Cannot prepare the cropped artwork for upload."));
                    return;
                }
                file->close();
                // Hold the owner until the daemon has completed the upload and artwork update.
                guard->call(
                    QStringLiteral("channel.artwork.set"),
                    {{"channel", id}, {"kind", kind}, {"file", file->fileName()}},
                    [guard, file, id, identity, generation, server, operation](const QJsonObject&) {
                        if (guard && guard->m_cropOperation == operation && guard->accountId() == identity
                            && guard->m_artworkCache.generation() == generation && guard->m_selectedServer == server)
                            emit guard->administrationFinished(QStringLiteral("channel.artwork.crop"), id, {});
                    },
                    tr("Cannot update channel image"),
                    [guard, file, id, identity, generation, server, operation](const QString& failure) {
                        if (guard && guard->m_cropOperation == operation && guard->accountId() == identity
                            && guard->m_artworkCache.generation() == generation && guard->m_selectedServer == server)
                            emit guard->administrationFinished(QStringLiteral("channel.artwork.crop"), id, failure);
                    });
            },
            Qt::QueuedConnection);
    });
}

} // namespace omachat::client
