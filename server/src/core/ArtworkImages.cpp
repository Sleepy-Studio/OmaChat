#include "ArtworkImages.hpp"

#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace omachat::server::artwork {
namespace {
constexpr qsizetype limit = 2 * 1024 * 1024;
bool validSize(QSize size)
{
    return size.width() > 0 && size.height() > 0 && size.width() <= 8192 && size.height() <= 8192
        && qint64(size.width()) * size.height() <= 16 * 1024 * 1024;
}
QByteArray encode(const QImage& image, const QByteArray& format)
{
    // QImage::copy/scaled retain text and colour profiles. Copy only pixels
    // into a fresh raster, including for images which need no resizing.
    const QImage pixels = image.convertToFormat(QImage::Format_RGBA8888);
    QImage clean(image.size(), QImage::Format_RGBA8888);
    if (pixels.isNull() || clean.isNull())
        return {};
    for (int y = 0; y < clean.height(); ++y)
        memcpy(clean.scanLine(y), pixels.constScanLine(y), size_t(clean.width()) * 4);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly))
        return {};
    QImageWriter writer(&buffer, format);
    writer.setQuality(90);
    if (!writer.write(clean) || bytes.isEmpty() || bytes.size() > limit)
        return {};
    return bytes;
}
} // namespace

bool validFraming(const proto::ArtworkFraming& f)
{
    if ((f.kind() != proto::ArtworkFraming::ICON && f.kind() != proto::ArtworkFraming::BANNER)
        || f.source_width() > 8192 || f.source_height() > 8192
        || !validSize(QSize(int(f.source_width()), int(f.source_height()))) || !f.width() || !f.height()
        || f.x() > f.source_width() || f.y() > f.source_height() || f.width() > f.source_width() - f.x()
        || f.height() > f.source_height() - f.y())
        return false;
    const double ratio = f.kind() == proto::ArtworkFraming::ICON ? 1.0 : 4.0;
    const double base = std::min(double(f.source_width()), f.source_height() * ratio);
    // Match the UI's floor rounding, including one-pixel crops of thin sources.
    const double expectedHeight = std::max(1.0, std::floor(double(f.width()) / ratio));
    return double(f.width()) <= base && double(f.width()) >= std::max(1.0, std::floor(base / 4.0))
        && f.height() == expectedHeight;
}

Images prepare(const QByteArray& bytes, const proto::ArtworkFraming* framing)
{
    Images out;
    if (bytes.isEmpty() || bytes.size() > limit || (framing && !validFraming(*framing))) {
        out.error = QStringLiteral("invalid or oversized artwork framing");
        return out;
    }
    QBuffer input;
    input.setData(bytes);
    input.open(QIODevice::ReadOnly);
    QImageReader reader(&input);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    const QByteArray format = reader.format().toLower();
    if ((format != "png" && format != "jpeg" && format != "webp") || !validSize(reader.size())
        || reader.supportsAnimation() || reader.imageCount() > 1) {
        out.error = QStringLiteral("artwork must be a bounded static PNG, JPEG, or WebP image");
        return out;
    }
    QImage image = reader.read();
    if (image.isNull() || !validSize(image.size())) {
        out.error = QStringLiteral("could not decode artwork within its dimension limits");
        return out;
    }
    if (framing) {
        if (image.size() != QSize(int(framing->source_width()), int(framing->source_height()))) {
            out.error = QStringLiteral("artwork source dimensions changed");
            return out;
        }
        out.source = encode(image, image.hasAlphaChannel() ? "PNG" : "JPEG");
        if (out.source.isEmpty()) {
            out.error = QStringLiteral("the metadata-free source exceeds the 2 MiB limit");
            return out;
        }
        image = image.copy(int(framing->x()), int(framing->y()), int(framing->width()), int(framing->height()));
        const bool icon = framing->kind() == proto::ArtworkFraming::ICON;
        image
            = image.scaled(icon ? QSize(512, 512) : QSize(1024, 256), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        out.small = encode(
            image.scaled(icon ? QSize(128, 128) : QSize(512, 128), Qt::IgnoreAspectRatio, Qt::SmoothTransformation),
            "PNG");
    } else if (image.width() > 1600 || image.height() > 1600) {
        image = image.scaled(1600, 1600, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    out.format = framing || image.hasAlphaChannel() ? "PNG" : "JPEG";
    out.full = encode(image, out.format);
    if (out.full.isEmpty() || (framing && out.small.isEmpty()))
        out.error = QStringLiteral("artwork variants exceed the 2 MiB limit");
    return out;
}
} // namespace omachat::server::artwork
