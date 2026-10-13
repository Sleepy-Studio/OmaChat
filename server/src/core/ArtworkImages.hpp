#pragma once

#include "network.pb.h"
#include <QByteArray>
#include <QString>

namespace omachat::server::artwork {
struct Images {
    QByteArray full, small, source;
    QString error;
    QByteArray format;
};
bool validFraming(const proto::ArtworkFraming& framing);
// Bounded decoding and fresh raster encoding drop source metadata. No files
// or mutable server state are accessed by this operation.
Images prepare(const QByteArray& bytes, const proto::ArtworkFraming* framing);
} // namespace omachat::server::artwork
