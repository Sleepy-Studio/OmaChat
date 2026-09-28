#pragma once

#include "audio/AudioBackend.hpp"
#include "omachat/config/ClientConfig.hpp"
#include "voice/AudioProcessor.hpp"
#include "voice/JitterBuffer.hpp"
#include "voice/MediaTransport.hpp"
#include "voice/OpusCodec.hpp"
#include "voice/SpscRing.hpp"

#include <QObject>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace omachat::voice {

// Owns one live voice session:
//
//   PipeWire capture (RT) -> ring -> encoder thread [DSP, VAD/PTT, Opus] -> UDP
//   UDP -> per-speaker JitterBuffer -> mixer thread [Opus/FEC/PLC, gain, mix]
//        -> ring -> PipeWire playback (RT)
//
// Realtime callbacks only touch lock-free rings and atomics. Worker threads
// are woken with atomic wait/notify, so an idle session costs no polling.
class VoiceEngine : public QObject {
    Q_OBJECT
public:
    struct Settings {
        QString input = QStringLiteral("default");
        QString output = QStringLiteral("default");
        config::InputMode mode = config::InputMode::VoiceActivity;
        AudioProcessor::Settings dsp;
        int bitrate = 40000;
        bool fec = true;
        int jitterMinMs = 20;
        int jitterMaxMs = 200;
        double outputVolume = 1.0;
    };

    struct Session {
        QHostAddress server;
        quint16 port = 0;
        std::uint32_t streamId = 0;
        media::Key key{};
        std::uint64_t selfUserId = 0;
        int bitrate = 40000;
    };

    explicit VoiceEngine(audio::AudioBackend& backend, QObject* parent = nullptr);
    ~VoiceEngine() override;

    void applySettings(const Settings& s);
    const Settings& settings() const { return m_settings; }

    bool start(const Session& session, QString* error);
    void stop();
    bool active() const { return m_running.load(); }

    void setMuted(bool muted) { m_muted.store(muted); }
    void setDeafened(bool deafened) { m_deafened.store(deafened); }
    void setPushToTalk(bool pressed) { m_ptt.store(pressed); }
    bool pushToTalk() const { return m_ptt.load(); }
    bool transmitting() const { return m_transmitting.load(); }
    float inputLevelDb() const { return m_inputLevelDb.load(); }

    // Client-local gain for one remote user, 0.0 .. 2.0.
    void setUserGain(std::uint64_t userId, double gain);
    void removeSpeaker(std::uint64_t userId);

    QJsonObject statsJson() const;

    // Screen sharing rides the same media stream. The handler receives video
    // packets and keyframe requests (Qt thread); sendVideo() and
    // requestKeyframe() may be called from any thread while active().
    using VideoHandler = std::function<void(const media::Header& header, std::span<const std::uint8_t> payload)>;
    void setVideoHandler(VideoHandler handler) { m_videoHandler = std::move(handler); }
    bool sendVideo(std::uint32_t timestamp90k, std::span<const std::uint8_t> payload, bool keyframe);
    void requestKeyframe(std::uint32_t sourceStream);

    // Screen-share audio: payload is [frame number:4][Opus]. Played through
    // the mixer like a voice, but it never counts as speaking.
    bool sendScreenAudio(std::uint32_t timestamp48k, std::span<const std::uint8_t> payload);
    void pushScreenAudio(std::uint64_t userId, std::uint32_t timestamp48k, std::span<const std::uint8_t> payload);
    void removeScreenAudio(std::uint64_t userId);
    static constexpr std::uint64_t kScreenAudioKey = std::uint64_t{1} << 63; // user ids never set bit 63

signals:
    void speakingChanged(quint64 userId, bool speaking);
    void transmittingChanged(bool transmitting);
    void registeredChanged(bool registered);
    void failed(const QString& reason);

private:
    struct Speaker {
        JitterBuffer jitter;
        OpusVoiceDecoder decoder;
        std::atomic<float> gain{1.0f};
        bool speaking = false;
        int framesSinceVoice = 1000;
        Speaker(int minMs, int maxMs)
            : jitter(minMs, maxMs)
        {
        }
    };

    void onCapture(const float* samples, std::size_t count);
    void onPlayback(float* out, std::size_t count);
    void encoderLoop();
    void mixerLoop();
    void onPacket(const media::Header& h, std::span<const std::uint8_t> payload);

    audio::AudioBackend& m_backend;
    MediaTransport m_transport;
    VideoHandler m_videoHandler;
    Settings m_settings;
    std::uint64_t m_selfUserId = 0;

    SpscRing<float> m_captureRing{48000}; // 1 s headroom
    SpscRing<float> m_playbackRing{48000};
    std::atomic<bool> m_captureReady{false};
    std::atomic<bool> m_playbackNeeded{false};
    std::atomic<std::uint32_t> m_lastQuantum{960};

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_muted{false};
    std::atomic<bool> m_deafened{false};
    std::atomic<bool> m_ptt{false};
    std::atomic<bool> m_transmitting{false};
    std::atomic<float> m_inputLevelDb{-120.0f};
    std::atomic<int> m_mode{0};
    std::atomic<float> m_outputVolume{1.0f};
    std::atomic<bool> m_settingsDirty{false};

    std::mutex m_speakersMutex;
    std::map<std::uint64_t, std::unique_ptr<Speaker>> m_speakers;
    std::map<std::uint64_t, float> m_gains;

    std::mutex m_settingsMutex; // guards m_settings copy used by the encoder
    std::thread m_encoder;
    std::thread m_mixer;
    std::atomic<std::uint64_t> m_underruns{0};
    std::atomic<std::uint64_t> m_framesSent{0};
};

} // namespace omachat::voice
