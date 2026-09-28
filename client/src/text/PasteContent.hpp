#pragma once

#include <QList>
#include <QString>
#include <QUrl>

class QMimeData;

namespace omachat::client {

// Decides what a paste into the composer means and turns a clipboard image
// into a file the daemon can upload. Pure apart from writing that file.
struct PasteContent {
    enum class Kind { Text, Files, Image };

    // Local files (copied in a file manager) win; an image counts only when
    // the clipboard carries no plain text, because spreadsheets and office
    // apps put a rendered image next to the text the user actually copied.
    static Kind classify(const QMimeData& mime);
    static QList<QUrl> localFiles(const QMimeData& mime);

    // Writes the image to `dir` as pasted-<timestamp>.<ext> and returns the
    // path, or an empty string. PNG, JPEG, GIF and WebP are kept byte for
    // byte (animation, quality); any other image is re-encoded as PNG.
    static QString saveImage(const QMimeData& mime, const QString& dir);
};

} // namespace omachat::client
