#pragma once

#include "omachat/media/FrameBuffer.hpp"
#include "omachat/media/VideoFragments.hpp"
#include "video/H264Codec.hpp"
#include "video/ScreenAudio.hpp"
#include "video/ScreenSource.hpp"
#include "voice/SpscRing.hpp"
#include "voice/VoiceEngine.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QTimer>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace omachat::video {

// Sending: capture -> newest frame slot -> encoder thread [scale, H.264]
//   -> fragments -> paced UDP through the voice media stream.
// Keyframes are produced on request (a viewer started or lost packets); the
// last captured frame is re-encoded when the screen is idle, because
// PipeWire only delivers frames when something changed.
class ScreenShare : public QObject {
    Q_OBJECT
public:
    // `audio` may be null: a share without sound. `previewPath`, if not
    // empty, gets the raw captured frames so the sharer can see their own
    // screen without a full encode/decode round trip.
    ScreenShare(std::unique_ptr<ScreenSource> source, std::unique_ptr<ScreenAudioSource> audio,
        const H264Encoder::Settings& settings, voice::VoiceEngine& voice, QString previewPath,
        QObject* parent = nullptr);
    ~ScreenShare() override;

    void start();
    void stop();
    void requestKeyframe(); // any thread; rate-limited
    // Sends (or clears, when `active` is false) an ephemeral pointer
    // position, normalized to the captured frame; any thread.
    void sendPointer(bool active, double x, double y);
    QJsonObject statsJson() const;
    const QString& previewPath() const { return m_previewPath; }

signals:
    void started();
    void failed(const QString& reason);
    void ended();

private:
    void encoderLoop();
    void audioLoop();
    void sendFrame(const std::vector<std::uint8_t>& au, bool keyframe, std::uint32_t ts90k);

    std::unique_ptr<ScreenSource> m_source;
    std::unique_ptr<ScreenAudioSource> m_audio;
    voice::SpscRing<float> m_audioRing{48000}; // 1 s; capture thread -> audio thread
    std::thread m_audioThread;
    std::atomic<bool> m_audioLive{false};
    QString m_audioError;
    std::uint64_t m_audioFrames = 0; // audio thread writes, read under m_statsMutex
    H264Encoder::Settings m_settings;
    voice::VoiceEngine& m_voice;
    QString m_previewPath;
    media::FrameBufferWriter m_previewWriter;

    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::vector<std::uint8_t> m_latest; // newest captured frame, tightly packed
    int m_latestW = 0, m_latestH = 0;
    PixelFormat m_latestFormat = PixelFormat::BGRx;
    bool m_fresh = false;
    bool m_stop = false;
    std::atomic<bool> m_keyframeWanted{true};
    std::thread m_encoder;
    std::uint32_t m_frameNumber; // encoder thread only
    std::chrono::steady_clock::time_point m_paceNext{};

    // stats (encoder thread writes, Qt thread reads)
    mutable std::mutex m_statsMutex;
    QString m_encoderName;
    int m_width = 0, m_height = 0;
    std::uint64_t m_frames = 0, m_keyframes = 0, m_bytes = 0, m_sendDrops = 0;
};

// Receiving one streamer: fragments (Qt thread) -> frames -> decode thread
// -> shared frame buffer the GUI maps.
class StreamViewer {
public:
    StreamViewer(std::uint64_t userId, QString framePath);
    ~StreamViewer();

    bool open(QString* error);
    void onPacket(const media::Header& header, std::span<const std::uint8_t> payload);
    void onPointer(std::span<const std::uint8_t> payload);
    // True when a keyframe should be requested now (rate-limited).
    bool wantsKeyframe();
    std::uint32_t sourceStream() const { return m_sourceStream; }
    const QString& path() const { return m_path; }
    std::uint64_t userId() const { return m_userId; }
    QJsonObject statsJson() const;

private:
    void decodeLoop();

    std::uint64_t m_userId;
    QString m_path;
    std::uint32_t m_sourceStream = 0;
    media::FrameAssembler m_assembler;
    media::FrameBufferWriter m_writer;
    H264Decoder m_decoder;
    std::int64_t m_lastKeyframeRequestMs = 0;
    std::uint64_t m_frames = 0; // assembled

    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<media::FrameAssembler::Frame> m_queue;
    bool m_stop = false;
    std::atomic<bool> m_decodeFailed{false};
    std::atomic<std::uint64_t> m_decoded{0};
    std::atomic<int> m_width{0}, m_height{0};
    std::thread m_decoder_thread;
    // Wall-clock milliseconds (QDateTime::currentMSecsSinceEpoch), 0 until
    // the first event; lets the GUI tell "never connected" from "stalled".
    std::atomic<std::int64_t> m_lastPacketMs{0};
    std::atomic<std::int64_t> m_lastDecodedMs{0};

    // Latest ephemeral pointer position from the sharer, 0..65535 over 0..1.
    std::atomic<bool> m_pointerActive{false};
    std::atomic<std::uint16_t> m_pointerX{0}, m_pointerY{0};
    std::atomic<std::int64_t> m_pointerAtMs{0};
};

// Everything screen sharing for the daemon: at most one outgoing share and
// any number of watched streams, all on the current voice session.
class VideoManager : public QObject {
    Q_OBJECT
public:
    using SourceFactory = std::function<std::unique_ptr<ScreenSource>()>;

    explicit VideoManager(voice::VoiceEngine& voice, QObject* parent = nullptr);
    ~VideoManager() override;

    void setSettings(const H264Encoder::Settings& s) { m_settings = s; }
    void setSourceFactory(SourceFactory f) { m_factory = std::move(f); }
    using AudioFactory = std::function<std::unique_ptr<ScreenAudioSource>()>;
    void setAudioFactory(AudioFactory f) { m_audioFactory = std::move(f); }
    void setShareAudio(bool enabled) { m_shareAudio = enabled; }
    void setFrameDirectory(const QString& dir) { m_frameDir = dir; }

    // Starts capturing and sending; `done` runs once with the outcome
    // (the desktop picker may take as long as the user needs).
    void startSharing(std::function<void(bool ok, const QString& error)> done);
    void stopSharing();
    bool sharing() const { return m_share != nullptr && m_shareLive; }
    // The frame file carrying the sharer's own picture-in-picture preview;
    // empty until sharing starts.
    QString selfPreviewPath() const { return m_share ? m_share->previewPath() : QString(); }
    // No-op when not currently sharing (nothing to point at).
    void sendPointer(bool active, double x, double y)
    {
        if (m_share)
            m_share->sendPointer(active, x, y);
    }

    // Returns the frame file the GUI should map.
    std::optional<QString> watch(std::uint64_t userId, QString* error);
    void unwatch(std::uint64_t userId);
    bool watching(std::uint64_t userId) const { return m_viewers.contains(userId); }

    void stopAll(); // voice ended
    QJsonArray watchingJson() const;
    QJsonObject statsJson() const;

signals:
    void sharingEnded(); // the desktop stopped it, or capture failed mid-way
    void changed();

private:
    void onVideoPacket(const media::Header& header, std::span<const std::uint8_t> payload);

    voice::VoiceEngine& m_voice;
    H264Encoder::Settings m_settings;
    SourceFactory m_factory;
    AudioFactory m_audioFactory;
    bool m_shareAudio = true;
    QString m_frameDir;
    std::unique_ptr<ScreenShare> m_share;
    bool m_shareLive = false;
    std::map<std::uint64_t, std::unique_ptr<StreamViewer>> m_viewers;
    QTimer m_keyframeTimer;
};

} // namespace omachat::video
