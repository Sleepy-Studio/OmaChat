#include "ArtworkCrop.hpp"

#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace omachat::client::artwork {
namespace {
bool validSize(QSize size)
{
    return size.width() > 0 && size.height() > 0 && size.width() <= 8192 && size.height() <= 8192
        && qint64(size.width()) * size.height() <= 16 * 1024 * 1024;
}
QByteArray png(const QImage& image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG"))
        return {};
    return bytes;
}
} // namespace

QImage readSource(const QString& path, QString* error)
{
    const QFileInfo info(path);
    if (!info.isFile() || info.size() <= 0 || info.size() > maxSourceBytes) {
        *error = QStringLiteral("Choose a nonempty local image no larger than 2 MiB.");
        return {};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Cannot read this local image.");
        return {};
    }
    QByteArray bytes = file.read(maxSourceBytes + 1);
    if (bytes.isEmpty() || bytes.size() > maxSourceBytes || !file.atEnd()) {
        *error = QStringLiteral("Image exceeds the 2 MiB artwork limit.");
        return {};
    }
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    const QByteArray format = reader.format().toLower();
    if (format != "png" && format != "jpeg" && format != "jpg" && format != "webp") {
        *error = QStringLiteral("Choose a PNG, JPEG or WebP image.");
        return {};
    }
    if (reader.supportsAnimation() || reader.imageCount() > 1) {
        *error = QStringLiteral("Choose a static image for artwork.");
        return {};
    }
    if (!validSize(reader.size())) {
        *error = QStringLiteral("Image dimensions exceed 8192 pixels or 16 megapixels.");
        return {};
    }
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull() || !validSize(image.size())) {
        *error = QStringLiteral("Cannot decode this image within the supported dimensions.");
        return {};
    }
    return image;
}

QRect cropRect(QSize source, const QString& kind, double focalX, double focalY, double zoom)
{
    if (!validSize(source) || (kind != u"icon" && kind != u"banner") || !std::isfinite(focalX) || !std::isfinite(focalY)
        || !std::isfinite(zoom) || zoom < 1 || zoom > 4)
        return {};
    const double ratio = kind == u"icon" ? 1.0 : 4.0;
    const double width = std::min(double(source.width()), source.height() * ratio) / zoom;
    const int w = std::max(1, int(std::floor(width)));
    const int h = std::max(1, int(std::floor(width / ratio)));
    // Focal controls traverse the entire available crop travel, including edges.
    const int x = qRound(std::clamp(focalX, 0.0, 1.0) * (source.width() - w));
    const int y = qRound(std::clamp(focalY, 0.0, 1.0) * (source.height() - h));
    return {x, y, w, h};
}

QByteArray encodeCrop(
    const QImage& source, const QString& kind, double focalX, double focalY, double zoom, QString* error)
{
    const QRect rect = cropRect(source.size(), kind, focalX, focalY, zoom);
    if (rect.isEmpty()) {
        *error = QStringLiteral("Invalid crop settings.");
        return {};
    }
    const QSize output = kind == u"icon" ? QSize(512, 512) : QSize(1024, 256);
    // Drop source metadata and normalize to a bounded raster before encoding.
    QImage normalized(output, QImage::Format_RGBA8888);
    normalized.fill(Qt::transparent);
    const QImage scaled = source.copy(rect)
                              .scaled(output, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                              .convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < output.height(); ++y)
        memcpy(normalized.scanLine(y), scaled.constScanLine(y), size_t(output.width()) * 4);
    QByteArray bytes = png(normalized);
    if (bytes.isEmpty() || bytes.size() > maxSourceBytes) {
        *error = QStringLiteral("Cannot encode this crop within the 2 MiB artwork limit.");
        return {};
    }
    return bytes;
}

QString fingerprint(const QImage& source)
{
    const QImage canonical = source.convertToFormat(QImage::Format_RGBA8888);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray::number(source.width()) + ':' + QByteArray::number(source.height()) + ':');
    for (int y = 0; y < canonical.height(); ++y)
        hash.addData(QByteArrayView(reinterpret_cast<const char*>(canonical.constScanLine(y)), canonical.width() * 4));
    return QString::fromLatin1(hash.result().toHex());
}

QString previewUrl(const QImage& source)
{
    return QStringLiteral("data:image/png;base64,")
        + QString::fromLatin1(png(source.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation)).toBase64());
}
} // namespace omachat::client::artwork
