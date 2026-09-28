#include "voice/VoiceEngine.hpp"

#include "omachat/core/Log.hpp"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <array>
#include <cmath>

namespace omachat::voice {

using config::InputMode;

VoiceEngine::VoiceEngine(audio::AudioBackend& backend, QObject* parent)
    : QObject(parent)
    , m_backend(backend)
    , m_transport(this)
{
    m_transport.setReceiver([this](const media::Header& h, std::span<const std::uint8_t> p) { onPacket(h, p); });
    connect(&m_transport, &MediaTransport::registeredChanged, this, &VoiceEngine::registeredChanged);
    connect(&m_backend, &audio::AudioBackend::streamFailed, this, [this](const QString& reason) {
        if (active()) {
            OMA_WARN("voice", "audio stream failed", {"reason", reason});
            emit failed(reason);
        }
    });
}

VoiceEngine::~VoiceEngine()
{
    stop();
}

void VoiceEngine::applySettings(const Settings& s)
{
    {
        std::lock_guard lock(m_settingsMutex);
        m_settings = s;
    }
    m_mode.store(static_cast<int>(s.mode));
    m_outputVolume.store(static_cast<float>(std::clamp(s.outputVolume, 0.0, 2.0)));
    m_settingsDirty.store(true);
    std::lock_guard lock(m_speakersMutex);
    for (auto& [id, sp] : m_speakers)
        sp->jitter.setLimits(s.jitterMinMs, s.jitterMaxMs);
}

bool VoiceEngine::start(const Session& session, QString* error)
{
    stop();
    m_selfUserId = session.selfUserId;
    if (!m_transport.open(session.server, session.port, session.streamId, session.key, error))
        return false;

    m_captureRing.clear();
    m_playbackRing.clear();
    m_running.store(true);
    m_settingsDirty.store(true);
    m_encoder = std::thread([this] { encoderLoop(); });
    m_mixer = std::thread([this] { mixerLoop(); });

    Settings s;
    {
        std::lock_guard lock(m_settingsMutex);
        s = m_settings;
    }
    QString audioError;
    const bool audioOk = m_backend.startStreams(
        s.input, s.output, [this](const float* x, std::size_t n) { onCapture(x, n); },
        [this](float* out, std::size_t n) { onPlayback(out, n); }, &audioError);
    if (!audioOk) {
        // Keep the network session so the user still appears in the channel
        // and can pick another device; report the device problem explicitly.
        OMA_WARN("voice", "audio device unavailable", {"error", audioError});
        emit failed(audioError);
    }
    OMA_INFO("voice", "engine started", {"stream", session.streamId}, {"audio", m_backend.name()});
    return true;
}

void VoiceEngine::stop()
{
    if (!m_running.exchange(false))
        return;
    m_backend.stopStreams(); // RT callbacks stop before the workers exit
    m_captureReady.store(true);
    m_captureReady.notify_one();
    m_playbackNeeded.store(true);
    m_playbackNeeded.notify_one();
    if (m_encoder.joinable())
        m_encoder.join();
    if (m_mixer.joinable())
        m_mixer.join();
    m_transport.close();
    {
        std::lock_guard lock(m_speakersMutex);
        for (auto& [id, sp] : m_speakers) {
            if (sp->speaking)
                emit speakingChanged(id, false);
        }
        m_speakers.clear();
    }
    if (m_transmitting.exchange(false))
        emit transmittingChanged(false);
    OMA_INFO("voice", "engine stopped");
}

void VoiceEngine::setUserGain(std::uint64_t userId, double gain)
{
    const float g = static_cast<float>(std::clamp(gain, 0.0, 2.0));
    std::lock_guard lock(m_speakersMutex);
    m_gains[userId] = g;
    if (auto it = m_speakers.find(userId); it != m_speakers.end())
        it->second->gain.store(g);
}

void VoiceEngine::removeSpeaker(std::uint64_t userId)
{
    bool wasSpeaking = false;
    {
        std::lock_guard lock(m_speakersMutex);
        auto it = m_speakers.find(userId);
        if (it == m_speakers.end())
            return;
        wasSpeaking = it->second->speaking;
        m_speakers.erase(it);
    }
    if (wasSpeaking)
        emit speakingChanged(userId, false);
}

// --------------------------------------------------------- realtime side

void VoiceEngine::onCapture(const float* samples, std::size_t count)
{
    m_captureRing.push(samples, count);
    if (m_captureRing.size() >= static_cast<std::size_t>(kFrameSamples)) {
        m_captureReady.store(true, std::memory_order_release);
        m_captureReady.notify_one();
    }
}

void VoiceEngine::onPlayback(float* out, std::size_t count)
{
    const std::size_t got = m_playbackRing.pop(out, count);
    if (got < count) {
        std::fill(out + got, out + count, 0.0f);
        if (got > 0 || m_running.load(std::memory_order_relaxed))
            m_underruns.fetch_add(1, std::memory_order_relaxed);
    }
    m_lastQuantum.store(static_cast<std::uint32_t>(count), std::memory_order_relaxed);
    if (m_playbackRing.size() < static_cast<std::size_t>(kFrameSamples) + count) {
        m_playbackNeeded.store(true, std::memory_order_release);
        m_playbackNeeded.notify_one();
    }
}

// --------------------------------------------------------------- encoder

void VoiceEngine::encoderLoop()
{
    AudioProcessor processor;
    OpusVoiceEncoder encoder;
    std::array<float, kFrameSamples> frame{};
    std::array<std::uint8_t, kMaxPacketBytes> packet{};
    std::uint32_t timestamp = 0;
    bool wasTransmitting = false;

    while (m_running.load()) {
        m_captureReady.wait(false, std::memory_order_acquire);
        m_captureReady.store(false, std::memory_order_relaxed);
        if (!m_running.load())
            break;

        if (m_settingsDirty.exchange(false)) {
            std::lock_guard lock(m_settingsMutex);
            processor.configure(m_settings.dsp);
            encoder.setBitrate(m_settings.bitrate);
            encoder.setFec(m_settings.fec, m_settings.fec ? 5 : 0);
        }

        while (m_captureRing.size() >= static_cast<std::size_t>(kFrameSamples)) {
            m_captureRing.pop(frame.data(), frame.size());
            const auto result = processor.process(frame);
            m_inputLevelDb.store(result.levelDb, std::memory_order_relaxed);

            bool transmit = false;
            if (!m_muted.load() && !m_deafened.load()) {
                switch (static_cast<InputMode>(m_mode.load())) {
                case InputMode::PushToTalk:
                    transmit = m_ptt.load();
                    break;
                case InputMode::AlwaysTransmit:
                    transmit = true;
                    break;
                case InputMode::VoiceActivity:
                    transmit = result.voice;
                    break;
                }
            }

            if (transmit || wasTransmitting) {
                const std::size_t n = encoder.encode(frame, packet);
                if (n > 0) {
                    // The frame after a talk spurt carries the end marker so
                    // receivers can release their jitter buffer promptly.
                    m_transport.sendAudio(timestamp, std::span<const std::uint8_t>(packet.data(), n), !transmit);
                    m_framesSent.fetch_add(1, std::memory_order_relaxed);
                }
            }
            if (transmit != wasTransmitting) {
                wasTransmitting = transmit;
                m_transmitting.store(transmit);
                QMetaObject::invokeMethod(
                    this,
                    [this, transmit] {
                        emit transmittingChanged(transmit);
                        emit speakingChanged(m_selfUserId, transmit);
                    },
                    Qt::QueuedConnection);
            }
            timestamp += kFrameSamples;
        }
    }
}

// ----------------------------------------------------------------- mixer

void VoiceEngine::mixerLoop()
{
    std::array<float, kFrameSamples> mix{};
    std::array<float, kFrameSamples> pcm{};
    JitterBuffer::Slot slot;
    // Speaking state changes discovered this frame, published after unlock.
    std::vector<std::pair<std::uint64_t, bool>> changes;
    changes.reserve(16);

    while (m_running.load()) {
        m_playbackNeeded.wait(false, std::memory_order_acquire);
        m_playbackNeeded.store(false, std::memory_order_relaxed);
        if (!m_running.load())
            break;

        const std::size_t want = static_cast<std::size_t>(kFrameSamples) + m_lastQuantum.load();
        while (m_running.load() && m_playbackRing.size() < want) {
            mix.fill(0.0f);
            changes.clear();
            const bool deaf = m_deafened.load();
            {
                std::lock_guard lock(m_speakersMutex);
                for (auto& [userId, sp] : m_speakers) {
                    sp->jitter.pop(slot);
                    int produced = 0;
                    const std::span<const std::uint8_t> data(slot.data.data(), slot.size);
                    switch (slot.kind) {
                    case JitterBuffer::Kind::Packet:
                        produced = sp->decoder.decode(data, pcm);
                        break;
                    case JitterBuffer::Kind::Fec:
                        produced = sp->decoder.decodeFec(data, pcm);
                        break;
                    case JitterBuffer::Kind::Conceal:
                        produced = sp->decoder.conceal(pcm);
                        break;
                    case JitterBuffer::Kind::Silence:
                        break;
                    }
                    const bool voiced = slot.kind == JitterBuffer::Kind::Packet || slot.kind == JitterBuffer::Kind::Fec;
                    sp->framesSinceVoice = voiced ? 0 : sp->framesSinceVoice + 1;
                    const bool speaking = sp->framesSinceVoice < 10; // 200 ms hold
                    if (speaking != sp->speaking) {
                        sp->speaking = speaking;
                        changes.emplace_back(userId, speaking);
                    }
                    if (deaf || produced <= 0)
                        continue;
                    const float g = sp->gain.load(std::memory_order_relaxed);
                    for (int i = 0; i < produced && i < kFrameSamples; ++i)
                        mix[static_cast<std::size_t>(i)] += pcm[static_cast<std::size_t>(i)] * g;
                }
            }
            const float vol = m_outputVolume.load(std::memory_order_relaxed);
            for (float& x : mix) {
                // Gentle soft clip keeps several loud speakers from wrapping.
                const float y = x * vol;
                x = std::abs(y) < 0.9f ? y : std::tanh(y);
            }
            m_playbackRing.push(mix.data(), mix.size());
            for (const auto& [id, speaking] : changes) {
                QMetaObject::invokeMethod(
                    this, [this, id, speaking] { emit speakingChanged(id, speaking); }, Qt::QueuedConnection);
            }
        }
    }
}

// --------------------------------------------------------------- network

void VoiceEngine::onPacket(const media::Header& h, std::span<const std::uint8_t> payload)
{
    if (h.type != media::PacketType::Audio || h.senderId == 0 || h.senderId == m_selfUserId)
        return;
    std::lock_guard lock(m_speakersMutex);
    auto it = m_speakers.find(h.senderId);
    if (it == m_speakers.end()) {
        if (m_speakers.size() >= 64)
            return;
        int minMs = 20, maxMs = 200;
        {
            std::lock_guard slock(m_settingsMutex);
            minMs = m_settings.jitterMinMs;
            maxMs = m_settings.jitterMaxMs;
        }
        auto sp = std::make_unique<Speaker>(minMs, maxMs);
        if (auto g = m_gains.find(h.senderId); g != m_gains.end())
            sp->gain.store(g->second);
        it = m_speakers.emplace(h.senderId, std::move(sp)).first;
    }
    it->second->jitter.push(h.sequence, h.timestamp, payload, (h.flags & media::FlagEndOfSpeech) != 0);
}

QJsonObject VoiceEngine::statsJson() const
{
    const auto t = m_transport.stats();
    QJsonArray speakers;
    {
        auto& self = const_cast<VoiceEngine&>(*this);
        std::lock_guard lock(self.m_speakersMutex);
        for (const auto& [id, sp] : m_speakers) {
            const auto s = sp->jitter.stats();
            speakers.append(QJsonObject{{"user_id", QString::number(id)}, {"received", static_cast<double>(s.received)},
                {"late", static_cast<double>(s.late)}, {"lost", static_cast<double>(s.lost)},
                {"fec_recovered", static_cast<double>(s.recoveredFec)}, {"concealed", static_cast<double>(s.concealed)},
                {"skipped", static_cast<double>(s.skipped)}, {"jitter_ms", s.jitterMs},
                {"target_ms", s.targetFrames * JitterBuffer::kFrameMs},
                {"buffered_ms", s.bufferedFrames * JitterBuffer::kFrameMs}});
        }
    }
    const double outputMs = static_cast<double>(m_playbackRing.size()) / 48.0;
    return {{"active", active()}, {"registered", m_transport.registered()},
        {"packets_sent", static_cast<double>(t.sent)}, {"packets_received", static_cast<double>(t.received)},
        {"packets_rejected", static_cast<double>(t.rejected)},
        {"frames_sent", static_cast<double>(m_framesSent.load())},
        {"playback_underruns", static_cast<double>(m_underruns.load())}, {"playback_buffer_ms", outputMs},
        {"input_level_db", static_cast<double>(m_inputLevelDb.load())}, {"transmitting", transmitting()},
        {"speakers", speakers}};
}

} // namespace omachat::voice
