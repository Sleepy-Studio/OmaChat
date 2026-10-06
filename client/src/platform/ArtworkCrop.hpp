#pragma once

#include <QByteArray>
#include <QImage>
#include <QRect>
#include <QString>

namespace omachat::client::artwork {
constexpr qint64 maxSourceBytes = 2 * 1024 * 1024;
// Reads only supported raster images, validating advertised and decoded sizes.
QImage readSource(const QString& path, QString* error);
QRect cropRect(QSize source, const QString& kind, double focalX, double focalY, double zoom);
QByteArray encodeCrop(
    const QImage& source, const QString& kind, double focalX, double focalY, double zoom, QString* error);
QString fingerprint(const QImage& source);
QString previewUrl(const QImage& source);
} // namespace omachat::client::artwork
