#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace omachat::video {

// Sound that goes with a screen share: 48 kHz mono float, delivered in
// arbitrary chunk sizes on the source's own thread.
class ScreenAudioSource : public QObject {
    Q_OBJECT
public:
    using SamplesFn = std::function<void(const float* mono, std::size_t count)>;

    using QObject::QObject;
    ~ScreenAudioSource() override = default;

    virtual bool start(SamplesFn onSamples, QString* error) = 0;
    virtual void stop() = 0;
    virtual QString name() const = 0;
    virtual int applications() const { return 0; } // how many programs are being heard
};

// Every application's audio output except OmaChat's own: a PipeWire input
// node that is linked, port by port, to each other program's playback
// stream as they come and go. Linking into one mono port lets PipeWire mix
// and downmix; OmaChat's voice playback is never included, so people on the
// call do not hear themselves echoed back through the share.
std::unique_ptr<ScreenAudioSource> makeApplicationAudioSource(QObject* parent = nullptr);

// A steady tone, for tests and headless runs.
class ToneAudioSource : public ScreenAudioSource {
    Q_OBJECT
public:
    explicit ToneAudioSource(double hz, QObject* parent = nullptr);
    ~ToneAudioSource() override;
    bool start(SamplesFn onSamples, QString* error) override;
    void stop() override;
    QString name() const override { return QStringLiteral("tone"); }

private:
    double m_hz;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

} // namespace omachat::video
