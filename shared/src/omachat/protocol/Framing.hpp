#pragma once

#include <QByteArray>

#include <cstdint>
#include <optional>

namespace omachat::protocol {

// Reliable-stream framing: u32 big-endian length, then payload bytes.
inline constexpr std::uint32_t kMaxFrameBytes = 8u * 1024u * 1024u;
inline constexpr int kFrameHeaderBytes = 4;

QByteArray encodeFrame(const QByteArray& payload);

// Incremental decoder. Feed arbitrary chunks; pull complete frames.
class FrameDecoder {
public:
    explicit FrameDecoder(std::uint32_t maxFrameBytes = kMaxFrameBytes)
        : m_max(maxFrameBytes)
    {
    }

    void feed(const QByteArray& chunk) { m_buffer.append(chunk); }

    enum class Status { Frame, NeedMore, Oversized };

    // On Status::Frame, `out` holds the payload. On Oversized the stream is
    // unrecoverable and the connection must be closed.
    Status next(QByteArray& out);

    qsizetype buffered() const { return m_buffer.size() - m_offset; }

private:
    QByteArray m_buffer;
    qsizetype m_offset = 0;
    std::uint32_t m_max;
};

} // namespace omachat::protocol
