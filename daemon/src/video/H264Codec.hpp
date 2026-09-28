#pragma once

#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace omachat::video {

// Raw pixel layouts a capture source may deliver (32 bits per pixel).
enum class PixelFormat { BGRx, BGRA, RGBx, RGBA, xRGB, xBGR };

// H.264 for screen sharing through libavcodec. Tries hardware encoders
// first (NVENC, AMF) and falls back to libx264, always configured for low
// latency: no B-frames, constant bitrate, SPS/PPS repeated on every IDR so a
// viewer can start from any keyframe.
class H264Encoder {
public:
    struct Settings {
        int maxWidth = 1920;
        int maxHeight = 1080;
        int fps = 30;
        int bitrateKbps = 4000;
        QString encoder = QStringLiteral("auto"); // auto | nvenc | amf | x264
    };

    H264Encoder() = default;
    ~H264Encoder();
    H264Encoder(const H264Encoder&) = delete;
    H264Encoder& operator=(const H264Encoder&) = delete;

    // Opens for frames of the given source size; the output keeps the aspect
    // ratio within the configured maximum.
    bool open(int sourceWidth, int sourceHeight, PixelFormat format, const Settings& settings, QString* error);
    void close();
    bool isOpen() const { return m_ctx != nullptr; }
    bool matches(int sourceWidth, int sourceHeight, PixelFormat format) const;

    // Encodes one frame; `out` receives the Annex B access unit (may be empty
    // while the encoder buffers). `pts90k` is the 90 kHz capture clock.
    bool encode(const std::uint8_t* pixels, int stride, std::int64_t pts90k, bool forceKeyframe,
        std::vector<std::uint8_t>& out, bool& keyframe);

    QString name() const { return m_name; }
    int width() const { return m_width; }
    int height() const { return m_height; }

    // Encoders this libavcodec build offers, in preference order.
    static QStringList available();

private:
    AVCodecContext* m_ctx = nullptr;
    AVFrame* m_frame = nullptr;
    AVPacket* m_packet = nullptr;
    SwsContext* m_sws = nullptr;
    QString m_name;
    int m_srcWidth = 0, m_srcHeight = 0;
    PixelFormat m_srcFormat = PixelFormat::BGRx;
    int m_width = 0, m_height = 0;
};

// Low-delay H.264 decoding to BGRA frames no larger than the frame buffer
// allows (bigger streams are scaled down).
class H264Decoder {
public:
    using Sink = std::function<void(const std::uint8_t* bgra, int width, int height, int stride)>;

    H264Decoder() = default;
    ~H264Decoder();
    H264Decoder(const H264Decoder&) = delete;
    H264Decoder& operator=(const H264Decoder&) = delete;

    bool open(QString* error);
    void close();
    // Returns false when the data could not be decoded (ask for a keyframe).
    bool decode(std::span<const std::uint8_t> accessUnit, const Sink& sink);

private:
    AVCodecContext* m_ctx = nullptr;
    AVFrame* m_frame = nullptr;
    AVPacket* m_packet = nullptr;
    SwsContext* m_sws = nullptr;
    std::vector<std::uint8_t> m_bgra;
    int m_swsSrcW = 0, m_swsSrcH = 0, m_swsSrcFmt = -1, m_outW = 0, m_outH = 0;
};

} // namespace omachat::video
