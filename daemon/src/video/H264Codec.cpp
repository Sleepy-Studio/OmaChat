#include "video/H264Codec.hpp"

#include "omachat/core/Log.hpp"
#include "omachat/media/FrameBuffer.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cstring>

namespace omachat::video {

namespace {

AVPixelFormat avFormat(PixelFormat f)
{
    switch (f) {
    case PixelFormat::BGRx:
        return AV_PIX_FMT_BGR0;
    case PixelFormat::BGRA:
        return AV_PIX_FMT_BGRA;
    case PixelFormat::RGBx:
        return AV_PIX_FMT_RGB0;
    case PixelFormat::RGBA:
        return AV_PIX_FMT_RGBA;
    case PixelFormat::xRGB:
        return AV_PIX_FMT_0RGB;
    case PixelFormat::xBGR:
        return AV_PIX_FMT_0BGR;
    }
    return AV_PIX_FMT_BGR0;
}

QString avError(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

// Largest even size within the bounds that keeps the source aspect ratio.
void fit(int srcW, int srcH, int maxW, int maxH, int& w, int& h)
{
    double scale = std::min({1.0, static_cast<double>(maxW) / srcW, static_cast<double>(maxH) / srcH});
    w = std::max(2, static_cast<int>(srcW * scale) & ~1);
    h = std::max(2, static_cast<int>(srcH * scale) & ~1);
}

struct Candidate {
    const char* key; // settings name
    const char* codec; // libavcodec encoder name
};
constexpr Candidate kEncoders[] = {{"nvenc", "h264_nvenc"}, {"amf", "h264_amf"}, {"x264", "libx264"}};

void tune(AVCodecContext* ctx, const char* codec)
{
    const std::string name = codec;
    if (name == "h264_nvenc") {
        av_opt_set(ctx->priv_data, "preset", "p2", 0);
        av_opt_set(ctx->priv_data, "tune", "ull", 0);
        av_opt_set(ctx->priv_data, "rc", "cbr", 0);
        av_opt_set_int(ctx->priv_data, "zerolatency", 1, 0);
        av_opt_set_int(ctx->priv_data, "forced-idr", 1, 0);
    } else if (name == "h264_amf") {
        av_opt_set(ctx->priv_data, "usage", "ultralowlatency", 0);
        av_opt_set(ctx->priv_data, "rc", "cbr", 0);
    } else if (name == "libx264") {
        av_opt_set(ctx->priv_data, "preset", "veryfast", 0);
        av_opt_set(ctx->priv_data, "tune", "zerolatency", 0);
        av_opt_set_int(ctx->priv_data, "forced-idr", 1, 0);
    }
}

} // namespace

QStringList H264Encoder::available()
{
    QStringList out;
    for (const auto& c : kEncoders) {
        if (avcodec_find_encoder_by_name(c.codec))
            out << QString::fromLatin1(c.key);
    }
    return out;
}

H264Encoder::~H264Encoder()
{
    close();
}

void H264Encoder::close()
{
    avcodec_free_context(&m_ctx);
    av_frame_free(&m_frame);
    av_packet_free(&m_packet);
    sws_freeContext(m_sws);
    m_sws = nullptr;
    m_name.clear();
}

bool H264Encoder::matches(int sourceWidth, int sourceHeight, PixelFormat format) const
{
    return m_ctx && sourceWidth == m_srcWidth && sourceHeight == m_srcHeight && format == m_srcFormat;
}

bool H264Encoder::open(int sourceWidth, int sourceHeight, PixelFormat format, const Settings& s, QString* error)
{
    close();
    if (sourceWidth < 2 || sourceHeight < 2) {
        if (error)
            *error = QStringLiteral("empty capture");
        return false;
    }
    int w = 0, h = 0;
    fit(sourceWidth, sourceHeight, s.maxWidth, s.maxHeight, w, h);
    const int fps = std::clamp(s.fps, 5, 60);
    QString lastError = QStringLiteral("no H.264 encoder in this FFmpeg build");
    for (const auto& c : kEncoders) {
        if (s.encoder != u"auto" && s.encoder != QLatin1StringView(c.key))
            continue;
        const AVCodec* codec = avcodec_find_encoder_by_name(c.codec);
        if (!codec)
            continue;
        AVCodecContext* ctx = avcodec_alloc_context3(codec);
        ctx->width = w;
        ctx->height = h;
        ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        ctx->time_base = AVRational{1, 90000};
        ctx->framerate = AVRational{fps, 1};
        ctx->gop_size = fps * 10; // viewers ask for keyframes; this is only a backstop
        ctx->max_b_frames = 0;
        ctx->bit_rate = std::int64_t{std::clamp(s.bitrateKbps, 300, 20000)} * 1000;
        ctx->rc_max_rate = ctx->bit_rate;
        ctx->rc_buffer_size = static_cast<int>(ctx->bit_rate / fps * 2); // ~2 frames of VBV
        ctx->profile = AV_PROFILE_H264_MAIN;
        ctx->thread_count = 0;
        ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
        tune(ctx, c.codec);
        const int rc = avcodec_open2(ctx, codec, nullptr);
        if (rc < 0) {
            lastError = QStringLiteral("%1: %2").arg(QLatin1StringView(c.codec), avError(rc));
            OMA_INFO("video", "encoder unavailable", {"encoder", c.codec}, {"reason", avError(rc)});
            avcodec_free_context(&ctx);
            continue;
        }
        m_ctx = ctx;
        m_name = QString::fromLatin1(c.codec);
        break;
    }
    if (!m_ctx) {
        if (error)
            *error = lastError;
        return false;
    }
    m_frame = av_frame_alloc();
    m_frame->format = AV_PIX_FMT_YUV420P;
    m_frame->width = w;
    m_frame->height = h;
    m_packet = av_packet_alloc();
    if (av_frame_get_buffer(m_frame, 32) < 0) {
        close();
        if (error)
            *error = QStringLiteral("out of memory");
        return false;
    }
    m_sws = sws_getContext(sourceWidth, sourceHeight, avFormat(format), w, h, AV_PIX_FMT_YUV420P,
        w == sourceWidth ? SWS_POINT : SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_sws) {
        close();
        if (error)
            *error = QStringLiteral("unsupported capture format");
        return false;
    }
    m_srcWidth = sourceWidth;
    m_srcHeight = sourceHeight;
    m_srcFormat = format;
    m_width = w;
    m_height = h;
    OMA_INFO("video", "encoder open", {"encoder", m_name}, {"width", w}, {"height", h}, {"fps", fps},
        {"kbps", s.bitrateKbps});
    return true;
}

bool H264Encoder::encode(const std::uint8_t* pixels, int stride, std::int64_t pts90k, bool forceKeyframe,
    std::vector<std::uint8_t>& out, bool& keyframe)
{
    out.clear();
    keyframe = false;
    if (!m_ctx || av_frame_make_writable(m_frame) < 0)
        return false;
    const std::uint8_t* src[4] = {pixels, nullptr, nullptr, nullptr};
    const int srcStride[4] = {stride, 0, 0, 0};
    sws_scale(m_sws, src, srcStride, 0, m_srcHeight, m_frame->data, m_frame->linesize);
    m_frame->pts = pts90k;
    m_frame->pict_type = forceKeyframe ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
    if (forceKeyframe)
        m_frame->flags |= AV_FRAME_FLAG_KEY;
    else
        m_frame->flags &= ~AV_FRAME_FLAG_KEY;
    if (avcodec_send_frame(m_ctx, m_frame) < 0)
        return false;
    for (;;) {
        const int rc = avcodec_receive_packet(m_ctx, m_packet);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
            break;
        if (rc < 0)
            return false;
        out.insert(out.end(), m_packet->data, m_packet->data + m_packet->size);
        keyframe = keyframe || (m_packet->flags & AV_PKT_FLAG_KEY) != 0;
        av_packet_unref(m_packet);
    }
    return true;
}

// ------------------------------------------------------------------ decoder

H264Decoder::~H264Decoder()
{
    close();
}

void H264Decoder::close()
{
    avcodec_free_context(&m_ctx);
    av_frame_free(&m_frame);
    av_packet_free(&m_packet);
    sws_freeContext(m_sws);
    m_sws = nullptr;
    m_swsSrcFmt = -1;
}

bool H264Decoder::open(QString* error)
{
    close();
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        if (error)
            *error = QStringLiteral("no H.264 decoder in this FFmpeg build");
        return false;
    }
    m_ctx = avcodec_alloc_context3(codec);
    m_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    m_ctx->thread_count = 2;
    m_ctx->thread_type = FF_THREAD_SLICE; // frame threading would add a frame of delay
    if (const int rc = avcodec_open2(m_ctx, codec, nullptr); rc < 0) {
        if (error)
            *error = avError(rc);
        close();
        return false;
    }
    m_frame = av_frame_alloc();
    m_packet = av_packet_alloc();
    return true;
}

bool H264Decoder::decode(std::span<const std::uint8_t> au, const Sink& sink)
{
    if (!m_ctx || au.empty())
        return false;
    // libavcodec may read past the end: give it padded memory.
    if (av_new_packet(m_packet, static_cast<int>(au.size())) < 0)
        return false;
    std::memcpy(m_packet->data, au.data(), au.size());
    const int sent = avcodec_send_packet(m_ctx, m_packet);
    av_packet_unref(m_packet);
    if (sent < 0)
        return false;
    bool ok = true;
    for (;;) {
        const int rc = avcodec_receive_frame(m_ctx, m_frame);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
            break;
        if (rc < 0)
            return false;
        if (m_frame->decode_error_flags != 0 || (m_frame->flags & AV_FRAME_FLAG_CORRUPT))
            ok = false;
        const int w = m_frame->width, h = m_frame->height;
        if (w < 2 || h < 2) {
            av_frame_unref(m_frame);
            continue;
        }
        if (!m_sws || w != m_swsSrcW || h != m_swsSrcH || m_frame->format != m_swsSrcFmt) {
            sws_freeContext(m_sws);
            fit(w, h, static_cast<int>(media::kMaxFrameWidth), static_cast<int>(media::kMaxFrameHeight), m_outW,
                m_outH);
            m_sws = sws_getContext(w, h, static_cast<AVPixelFormat>(m_frame->format), m_outW, m_outH, AV_PIX_FMT_BGRA,
                SWS_BILINEAR, nullptr, nullptr, nullptr);
            m_swsSrcW = w;
            m_swsSrcH = h;
            m_swsSrcFmt = m_frame->format;
            m_bgra.assign(static_cast<std::size_t>(m_outW) * m_outH * 4, 0);
        }
        if (m_sws) {
            std::uint8_t* dst[4] = {m_bgra.data(), nullptr, nullptr, nullptr};
            const int dstStride[4] = {m_outW * 4, 0, 0, 0};
            sws_scale(m_sws, m_frame->data, m_frame->linesize, 0, h, dst, dstStride);
            sink(m_bgra.data(), m_outW, m_outH, m_outW * 4);
        }
        av_frame_unref(m_frame);
    }
    return ok;
}

} // namespace omachat::video
