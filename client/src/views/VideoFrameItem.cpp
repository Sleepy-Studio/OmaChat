#include "views/VideoFrameItem.hpp"

#include <QQuickWindow>
#include <QSGSimpleTextureNode>

namespace omachat::client {

VideoFrameItem::VideoFrameItem(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    m_timer.setInterval(16);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &VideoFrameItem::poll);
}

void VideoFrameItem::setSource(const QString& path)
{
    if (path == m_source)
        return;
    m_source = path;
    m_reader.close();
    m_sequence = 0;
    m_image = QImage();
    m_dirty = true;
    if (!m_source.isEmpty()) {
        m_reader.open(m_source); // retried by poll() until the daemon created it
        m_timer.start();
    } else {
        m_timer.stop();
    }
    emit sourceChanged();
    emit frameChanged();
    update();
}

void VideoFrameItem::poll()
{
    if (!m_reader.isOpen() && !m_reader.open(m_source))
        return;
    if (!isVisible() || !m_reader.readIfNewer(m_sequence, m_frame))
        return;
    const bool first = m_image.isNull();
    const bool sizeChanged
        = m_image.width() != static_cast<int>(m_frame.width) || m_image.height() != static_cast<int>(m_frame.height);
    m_sequence = m_frame.sequence;
    // BGRA bytes are Format_ARGB32 on little-endian machines. The image
    // takes a deep copy so the reader's buffer can be reused.
    m_image = QImage(m_frame.pixels.data(), static_cast<int>(m_frame.width), static_cast<int>(m_frame.height),
        static_cast<qsizetype>(m_frame.width) * 4, QImage::Format_RGB32)
                  .copy();
    m_dirty = true;
    if (first || sizeChanged)
        emit frameChanged();
    update();
}

QSGNode* VideoFrameItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*)
{
    auto* node = static_cast<QSGSimpleTextureNode*>(old);
    if (m_image.isNull()) {
        delete node;
        return nullptr;
    }
    if (!node) {
        node = new QSGSimpleTextureNode;
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Linear);
    }
    if (m_dirty) {
        node->setTexture(window()->createTextureFromImage(m_image));
        m_dirty = false;
    }
    // Letterbox: keep the stream's aspect ratio inside the item.
    const QSizeF frame = m_image.size();
    const qreal scale = std::min(width() / frame.width(), height() / frame.height());
    const QSizeF shown = frame * scale;
    node->setRect(
        QRectF((width() - shown.width()) / 2, (height() - shown.height()) / 2, shown.width(), shown.height()));
    return node;
}

} // namespace omachat::client
