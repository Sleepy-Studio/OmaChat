#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <cstddef>
#include <functional>
#include <mutex>

namespace omachat::audio {

struct AudioDevice {
    QString id; // stable node name used as the stream target
    QString description; // human readable
    bool input = false;
};

// Boundary between the voice engine and the platform audio system. Streams
// are always 48 kHz mono float32. The callbacks run on the backend's
// realtime thread and must be lock-free and allocation-free.
class AudioBackend : public QObject {
    Q_OBJECT
public:
    using CaptureFn = std::function<void(const float* samples, std::size_t count)>;
    using PlaybackFn = std::function<void(float* out, std::size_t count)>;

    using QObject::QObject;
    ~AudioBackend() override = default;

    virtual QString name() const = 0;
    virtual bool available() const = 0;
    virtual QList<AudioDevice> devices() const = 0;

    // `input`/`output` are device ids or "default".
    virtual bool startStreams(
        const QString& input, const QString& output, CaptureFn capture, PlaybackFn playback, QString* error) = 0;
    virtual void stopStreams() = 0;
    virtual bool streaming() const = 0;

signals:
    void devicesChanged();
    void streamFailed(const QString& reason);
};

// No audio hardware. Tests drive the callbacks directly via pump*().
class NullAudioBackend final : public AudioBackend {
    Q_OBJECT
public:
    using AudioBackend::AudioBackend;

    QString name() const override { return QStringLiteral("null"); }
    bool available() const override { return true; }
    QList<AudioDevice> devices() const override { return {}; }

    bool startStreams(const QString&, const QString&, CaptureFn capture, PlaybackFn playback, QString*) override
    {
        std::lock_guard lock(m_mutex);
        m_capture = std::move(capture);
        m_playback = std::move(playback);
        m_streaming = true;
        return true;
    }
    void stopStreams() override
    {
        std::lock_guard lock(m_mutex);
        m_capture = {};
        m_playback = {};
        m_streaming = false;
    }
    bool streaming() const override { return m_streaming; }

    // Simulate the device clock from a test.
    void pumpCapture(const float* samples, std::size_t count)
    {
        std::lock_guard lock(m_mutex);
        if (m_capture)
            m_capture(samples, count);
    }
    void pumpPlayback(float* out, std::size_t count)
    {
        std::lock_guard lock(m_mutex);
        if (m_playback)
            m_playback(out, count);
    }

private:
    std::mutex m_mutex;
    CaptureFn m_capture;
    PlaybackFn m_playback;
    bool m_streaming = false;
};

} // namespace omachat::audio
