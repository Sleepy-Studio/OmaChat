#pragma once

#include "audio/AudioBackend.hpp"

#include <map>
#include <memory>
#include <mutex>

namespace omachat::audio {

// PipeWire implementation. Device discovery follows the registry (hotplug
// is event-driven); capture/playback use RT-process streams with
// media.role=Communication and a 10 ms node latency request.
class PipeWireBackend final : public AudioBackend {
    Q_OBJECT
public:
    explicit PipeWireBackend(QObject* parent = nullptr);
    ~PipeWireBackend() override;

    QString name() const override { return QStringLiteral("pipewire"); }
    bool available() const override;
    QList<AudioDevice> devices() const override;

    bool startStreams(
        const QString& input, const QString& output, CaptureFn capture, PlaybackFn playback, QString* error) override;
    void stopStreams() override;
    bool streaming() const override;

    struct Impl;

private:
    std::unique_ptr<Impl> d;
};

} // namespace omachat::audio
