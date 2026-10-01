// Composer attachments, upload progress, image previews and saving files.
// The daemon does all transfers; this file only keeps the UI's view of them.

#include "controllers/AppController.hpp"

#include "omachat/core/Paths.hpp"
#include "text/PasteContent.hpp"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QMimeData>
#include <QMimeDatabase>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

namespace omachat::client {

namespace {

constexpr int kMaxFilesPerMessage = 10;
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
    for (const auto& t : m_uploads)
        out.append(t.toVariantMap());
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

void AppController::onTransferProgress(const QJsonObject& data)
{
    if (data.value(QStringLiteral("direction")).toString() != u"upload")
        return;
    const QString id = data.value(QStringLiteral("id")).toString();
    if (data.value(QStringLiteral("complete")).toBool() || data.contains(QStringLiteral("error")))
        m_uploads.remove(id);
    else
        m_uploads.insert(id, data);
    emit attachmentsChanged();
}

void AppController::cancelTransfer(const QString& transferId)
{
    call(QStringLiteral("transfer.cancel"), {{"id", transferId}});
}

void AppController::requestPreview(const QString& attachmentId, const QString& filename, double size)
{
    if (size > kMaxPreviewBytes || m_previews.contains(attachmentId) || m_previewRequests.contains(attachmentId))
        return;
    m_previewRequests.insert(attachmentId);
    m_link.request(
        QStringLiteral("attachment.download"),
        {{"attachment", attachmentId}, {"filename", filename}, {"to", "cache"}, {"size", size}},
        [this, attachmentId](const ipc::Reply& r) {
            if (!r.ok) {
                m_previewRequests.remove(attachmentId);
                return;
            }
            m_previews.insert(attachmentId, QUrl::fromLocalFile(r.result.value(QStringLiteral("path")).toString()));
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
    m_previewRequests.insert(attachmentId);
    m_link.request(
        QStringLiteral("attachment.download"), {{"attachment", attachmentId}, {"filename", filename}, {"to", "cache"}},
        [this, attachmentId](const ipc::Reply& r) {
            if (!r.ok) {
                m_previewRequests.remove(attachmentId);
                return;
            }
            m_previews.insert(attachmentId, QUrl::fromLocalFile(r.result.value(QStringLiteral("path")).toString()));
            emit previewsChanged();
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
    m_link.request(
        QStringLiteral("attachment.download"),
        {{"attachment", attachmentId}, {"filename", filename}, {"to", "cache"}, {"size", size}},
        [this, attachmentId, filename](const ipc::Reply& r) {
            if (!r.ok) {
                showNotice(tr("Cannot open %1: %2").arg(filename, r.errorMessage), true);
                return;
            }
            const QString path = r.result.value(QStringLiteral("path")).toString();
            if (safeToOpen(path))
                QDesktopServices::openUrl(QUrl::fromLocalFile(path));
            else
                saveAttachment(attachmentId, filename);
        },
        0);
}

void AppController::openVideoAttachment(const QString& attachmentId, const QString& filename)
{
    const QString mpv = QStandardPaths::findExecutable(QStringLiteral("mpv"));
    if (mpv.isEmpty()) {
        showNotice(tr("Install MPV to open video attachments."), true);
        return;
    }
    m_link.request(
        QStringLiteral("attachment.download"), {{"attachment", attachmentId}, {"filename", filename}, {"to", "cache"}},
        [this, mpv, filename](const ipc::Reply& r) {
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

} // namespace omachat::client
