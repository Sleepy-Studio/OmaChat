#include "omachat/protocol/Framing.hpp"

#include <QtEndian>

namespace omachat::protocol {

QByteArray encodeFrame(const QByteArray& payload)
{
    QByteArray out;
    out.resize(kFrameHeaderBytes + payload.size());
    qToBigEndian<quint32>(static_cast<quint32>(payload.size()), out.data());
    std::copy(payload.cbegin(), payload.cend(), out.begin() + kFrameHeaderBytes);
    return out;
}

FrameDecoder::Status FrameDecoder::next(QByteArray& out)
{
    const qsizetype available = m_buffer.size() - m_offset;
    if (available < kFrameHeaderBytes)
        return Status::NeedMore;

    const auto length = qFromBigEndian<quint32>(m_buffer.constData() + m_offset);
    if (length > m_max)
        return Status::Oversized;
    if (available < kFrameHeaderBytes + static_cast<qsizetype>(length))
        return Status::NeedMore;

    out = m_buffer.mid(m_offset + kFrameHeaderBytes, length);
    m_offset += kFrameHeaderBytes + length;

    // Compact occasionally instead of on every frame.
    if (m_offset == m_buffer.size()) {
        m_buffer.clear();
        m_offset = 0;
    } else if (m_offset > 64 * 1024) {
        m_buffer.remove(0, m_offset);
        m_offset = 0;
    }
    return Status::Frame;
}

} // namespace omachat::protocol
