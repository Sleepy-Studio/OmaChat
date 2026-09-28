#pragma once

#include "omachat/media/FrameBuffer.hpp"

#include <QImage>
#include <QQuickItem>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

namespace omachat::client {

// Shows a stream decoded by omachatd: maps the frame file whose path the
// daemon returned from stream.watch and draws the newest frame, scaled to
// fit with its aspect ratio kept. Polls once per display frame; copying
// only happens when the daemon published a new one.
class VideoFrameItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY frameChanged)
    Q_PROPERTY(QSize frameSize READ frameSize NOTIFY frameChanged)

public:
    explicit VideoFrameItem(QQuickItem* parent = nullptr);

    QString source() const { return m_source; }
    void setSource(const QString& path);
    bool hasFrame() const { return !m_image.isNull(); }
    QSize frameSize() const { return m_image.size(); }

signals:
    void sourceChanged();
    void frameChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;

private:
    void poll();

    QString m_source;
    media::FrameBufferReader m_reader;
    media::FrameBufferReader::Frame m_frame;
    std::uint64_t m_sequence = 0;
    QImage m_image;
    bool m_dirty = false;
    QTimer m_timer;
};

} // namespace omachat::client
