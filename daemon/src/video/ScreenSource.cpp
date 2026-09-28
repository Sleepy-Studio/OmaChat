#include "video/ScreenSource.hpp"

#include <QMetaObject>

#include <chrono>
#include <vector>

namespace omachat::video {

SyntheticScreenSource::SyntheticScreenSource(int width, int height, int fps, QObject* parent)
    : ScreenSource(parent)
    , m_width(width)
    , m_height(height)
    , m_fps(fps)
{
}

SyntheticScreenSource::~SyntheticScreenSource()
{
    stop();
}

void SyntheticScreenSource::start(FrameFn onFrame)
{
    stop();
    m_running = true;
    m_thread = std::thread([this, onFrame = std::move(onFrame)] {
        // Four colored quadrants with a white bar sweeping across: easy to
        // recognize after lossy coding, and every frame differs.
        std::vector<std::uint8_t> px(static_cast<std::size_t>(m_width) * m_height * 4);
        const auto interval = std::chrono::microseconds(1'000'000 / std::max(1, m_fps));
        auto next = std::chrono::steady_clock::now();
        for (int n = 0; m_running; ++n) {
            const int bar = (n * 8) % m_width;
            for (int y = 0; y < m_height; ++y) {
                for (int x = 0; x < m_width; ++x) {
                    std::uint8_t* p = &px[(static_cast<std::size_t>(y) * m_width + x) * 4];
                    const bool right = x >= m_width / 2, bottom = y >= m_height / 2;
                    const bool onBar = x >= bar && x < bar + 8;
                    p[0] = onBar ? 255 : (right ? 230 : 20);
                    p[1] = onBar ? 255 : (bottom ? 220 : 30);
                    p[2] = onBar ? 255 : ((right != bottom) ? 200 : 40);
                    p[3] = 255;
                }
            }
            onFrame(Frame{px.data(), m_width, m_height, m_width * 4, PixelFormat::BGRx});
            next += interval;
            std::this_thread::sleep_until(next);
        }
    });
    QMetaObject::invokeMethod(this, [this] { emit started(); }, Qt::QueuedConnection);
}

void SyntheticScreenSource::stop()
{
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
}

} // namespace omachat::video
