#include "text/PasteContent.hpp"

#include <QDateTime>
#include <QFileInfo>
#include <QImage>
#include <QMimeData>
#include <QSaveFile>

#include <algorithm>
#include <utility>

namespace omachat::client {

namespace {

constexpr std::pair<const char*, const char*> kRawImageFormats[]
    = {{"image/png", "png"}, {"image/jpeg", "jpg"}, {"image/gif", "gif"}, {"image/webp", "webp"}};

QString uniquePath(const QString& dir, const QString& extension)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString path = dir + QStringLiteral("/pasted-%1.%2").arg(stamp, extension);
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = dir + QStringLiteral("/pasted-%1-%2.%3").arg(stamp).arg(n).arg(extension);
    return path;
}

bool offersImage(const QMimeData& mime)
{
    return mime.hasImage() || std::ranges::any_of(kRawImageFormats, [&](const auto& f) {
        return mime.hasFormat(QString::fromLatin1(f.first));
    });
}

} // namespace

QList<QUrl> PasteContent::localFiles(const QMimeData& mime)
{
    if (!mime.hasUrls())
        return {};
    QList<QUrl> urls = mime.urls();
    if (urls.isEmpty() || !std::ranges::all_of(urls, [](const QUrl& u) { return u.isLocalFile(); }))
        return {};
    return urls;
}

PasteContent::Kind PasteContent::classify(const QMimeData& mime)
{
    if (!localFiles(mime).isEmpty())
        return Kind::Files;
    if (offersImage(mime) && mime.text().trimmed().isEmpty())
        return Kind::Image;
    return Kind::Text;
}

QString PasteContent::saveImage(const QMimeData& mime, const QString& dir)
{
    for (const auto& [type, extension] : kRawImageFormats) {
        const QString format = QString::fromLatin1(type);
        if (!mime.hasFormat(format))
            continue;
        const QByteArray bytes = mime.data(format);
        if (bytes.isEmpty() || QImage::fromData(bytes).isNull())
            continue; // mislabelled; try the next format
        const QString path = uniquePath(dir, QString::fromLatin1(extension));
        QSaveFile file(path);
        if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit())
            return path;
        return {};
    }
    const QImage image = qvariant_cast<QImage>(mime.imageData());
    if (image.isNull())
        return {};
    const QString path = uniquePath(dir, QStringLiteral("png"));
    return image.save(path, "PNG") ? path : QString();
}

} // namespace omachat::client
