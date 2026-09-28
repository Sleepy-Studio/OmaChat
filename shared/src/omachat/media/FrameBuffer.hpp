#pragma once

#include <QString>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace omachat::media {

// A decoded video frame handed from omachatd to the GUI through a file in
// $XDG_RUNTIME_DIR/omachat (mode 0600) that both processes map. One writer,
// any number of readers; a sequence lock means a reader never shows a
// half-written frame, and neither side ever waits for the other.
//
//   0   8  magic "OMAFRAME"
//   8   4  version (1)
//   12  4  capacity    bytes of pixel storage after the header
//   16  8  sequence    odd while a frame is being written
//   24  4  width
//   28  4  height
//   32  4  stride      bytes per row (width * 4)
//   36  4  reserved
//   40  8  timestamp   ms since epoch of the frame's arrival
//   64  …  pixels      BGRA, 8 bits per channel (QImage::Format_ARGB32 on little endian)
inline constexpr std::size_t kFrameHeaderBytes = 64;
inline constexpr std::uint32_t kMaxFrameWidth = 2560;
inline constexpr std::uint32_t kMaxFrameHeight = 1600;

class FrameBufferWriter {
public:
    FrameBufferWriter() = default;
    ~FrameBufferWriter();
    FrameBufferWriter(const FrameBufferWriter&) = delete;
    FrameBufferWriter& operator=(const FrameBufferWriter&) = delete;

    // Creates (or replaces) the file sized for the largest allowed frame.
    bool create(const QString& path, QString* error);
    void close(); // unmaps and deletes the file
    const QString& path() const { return m_path; }

    // Publishes one BGRA frame. Frames larger than kMaxFrame* are refused.
    bool write(const std::uint8_t* pixels, std::uint32_t width, std::uint32_t height, std::uint32_t stride);

private:
    QString m_path;
    std::uint8_t* m_map = nullptr;
    std::size_t m_size = 0;
};

class FrameBufferReader {
public:
    FrameBufferReader() = default;
    ~FrameBufferReader();
    FrameBufferReader(const FrameBufferReader&) = delete;
    FrameBufferReader& operator=(const FrameBufferReader&) = delete;

    bool open(const QString& path);
    void close();
    bool isOpen() const { return m_map != nullptr; }

    struct Frame {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint64_t sequence = 0;
        std::vector<std::uint8_t> pixels; // tightly packed, width * 4 per row
    };
    // Copies the newest frame if it is newer than `lastSequence`. Returns
    // false when there is nothing new (or the writer was mid-frame).
    bool readIfNewer(std::uint64_t lastSequence, Frame& out) const;

private:
    const std::uint8_t* m_map = nullptr;
    std::size_t m_size = 0;
};

} // namespace omachat::media
