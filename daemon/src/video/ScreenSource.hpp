#pragma once

#include "video/H264Codec.hpp"

#include <QObject>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace omachat::video {

// Where shared screen frames come from. start() is asynchronous: the
// portal source first shows the desktop's own picker. Frames are delivered
// on the source's thread; the callback must copy what it keeps.
class ScreenSource : public QObject {
    Q_OBJECT
public:
    struct Frame {
        const std::uint8_t* data = nullptr;
        int width = 0;
        int height = 0;
        int stride = 0;
        PixelFormat format = PixelFormat::BGRx;
    };
    using FrameFn = std::function<void(const Frame&)>;

    using QObject::QObject;
    ~ScreenSource() override = default;

    virtual void start(FrameFn onFrame) = 0;
    virtual void stop() = 0;
    virtual QString name() const = 0;

signals:
    void started();
    void failed(const QString& reason); // before started(): cancelled or unavailable
    void ended(); // the user stopped sharing from the desktop side
};

// xdg-desktop-portal ScreenCast + PipeWire (shared-memory buffers).
std::unique_ptr<ScreenSource> makePortalSource(QObject* parent = nullptr);

// A moving test pattern, for tests and machines without a portal.
class SyntheticScreenSource : public ScreenSource {
    Q_OBJECT
public:
    SyntheticScreenSource(int width, int height, int fps, QObject* parent = nullptr);
    ~SyntheticScreenSource() override;
    void start(FrameFn onFrame) override;
    void stop() override;
    QString name() const override { return QStringLiteral("synthetic"); }

private:
    int m_width, m_height, m_fps;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

} // namespace omachat::video
