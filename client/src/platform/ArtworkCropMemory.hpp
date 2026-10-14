#pragma once

#include <QString>
#include <optional>

namespace omachat::client::artwork {
struct CropChoice {
    double x = 0.5, y = 0.5, zoom = 1;
};
// Device-local preferences only. The key hashes endpoint/account, server,
// canonical pixels and slot; neither source paths nor identity text is stored.
QString cropMemoryKey(const QString& identity, const QString& server, const QString& pixels, const QString& kind);
// Call on a worker. Operations are bounded and serialized across processes.
std::optional<CropChoice> loadCropChoice(const QString& directory, const QString& key);
bool saveCropChoice(const QString& directory, const QString& key, CropChoice choice);
} // namespace omachat::client::artwork
