#include "video/VideoManager.hpp"

#include "omachat/core/Log.hpp"
#include "omachat/core/Paths.hpp"

#include <QDateTime>
#include <QDir>
#include <QPointer>
#include <QRandomGenerator>

#include <chrono>

namespace omachat::video {

namespace {

using Clock = std::chrono::steady_clock;

std::int64_t nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

// Frames older than this are dropped rather than decoded late.
constexpr std::size_t kMaxQueuedFrames = 6;
// A new keyframe at most this often, however many viewers ask.
constexpr std::int64_t kMinKeyframeIntervalMs = 300;
constexpr std::int64_t kKeyframeRetryMs = 500;

} // namespace

// ============================================================== ScreenShare

ScreenShare::ScreenShare(std::unique_ptr<ScreenSource> source, const H264Encoder::Settings& settings,
    voice::VoiceEngine& voice, QObject* parent)
    : QObject(parent)
    , m_source(std::move(source))
    , m_settings(settings)
    , m_voice(voice)
    , m_frameNumber(QRandomGenerator::global()->generate()) // viewers never mistake a new share for old frames
{
    connect(m_source.get(), &ScreenSource::started, this, &ScreenShare::started);
    connect(m_source.get(), &ScreenSource::failed, this, &ScreenShare::failed);
    connect(m_source.get(), &ScreenSource::ended, this, &ScreenShare::ended);
}

ScreenShare::~ScreenShare()
{
    stop();
}

void ScreenShare::start()
{
    m_stop = false;
    m_encoder = std::thread([this] { encoderLoop(); });
    m_source->start([this](const ScreenSource::Frame& f) {
        // Capture thread: copy into the newest-frame slot and wake the encoder.
        std::lock_guard lock(m_mutex);
        const std::size_t row = static_cast<std::size_t>(f.width) * 4;
        m_latest.resize(row * f.height);
        for (int y = 0; y < f.height; ++y)
            std::memcpy(m_latest.data() + row * y, f.data + static_cast<std::size_t>(f.stride) * y, row);
        m_latestW = f.width;
        m_latestH = f.height;
        m_latestFormat = f.format;
        m_fresh = true;
        m_wake.notify_one();
    });
}

void ScreenShare::stop()
{
    if (m_source)
        m_source->stop();
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_one();
    if (m_encoder.joinable())
        m_encoder.join();
}

void ScreenShare::requestKeyframe()
{
    m_keyframeWanted = true;
    m_wake.notify_one();
}

void ScreenShare::encoderLoop()
{
    H264Encoder encoder;
    std::vector<std::uint8_t> frame;
    int width = 0, height = 0;
    PixelFormat format = PixelFormat::BGRx;
    const auto interval = std::chrono::microseconds(1'000'000 / std::clamp(m_settings.fps, 5, 60));
    auto lastEncode = Clock::now() - interval;
    auto lastKeyframe = Clock::now() - std::chrono::seconds(10);
    const auto start = Clock::now();

    for (;;) {
        std::unique_lock lock(m_mutex);
        m_wake.wait_for(lock, std::chrono::milliseconds(250),
            [&] { return m_stop || m_fresh || (m_keyframeWanted && (!frame.empty() || !m_latest.empty())); });
        if (m_stop)
            return;
        // Keep to the frame rate: a burst of captures becomes one encode.
        if (Clock::now() - lastEncode < interval) {
            lock.unlock();
            std::this_thread::sleep_until(lastEncode + interval);
            lock.lock();
            if (m_stop)
                return;
        }
        if (m_fresh) {
            std::swap(frame, m_latest); // the capture thread refills the old buffer
            width = m_latestW;
            height = m_latestH;
            format = m_latestFormat;
            m_fresh = false;
        }
        lock.unlock();
        if (frame.empty())
            continue;

        bool keyframe = false;
        if (m_keyframeWanted && Clock::now() - lastKeyframe >= std::chrono::milliseconds(kMinKeyframeIntervalMs))
            keyframe = m_keyframeWanted.exchange(false);
        if (!encoder.matches(width, height, format)) {
            QString error;
            if (!encoder.open(width, height, format, m_settings, &error)) {
                OMA_WARN("video", "cannot encode the screen", {"reason", error});
                QPointer<ScreenShare> guard(this);
                QMetaObject::invokeMethod(
                    this,
                    [guard, error] {
                        if (guard)
                            emit guard->ended();
                    },
                    Qt::QueuedConnection);
                return;
            }
            std::lock_guard stats(m_statsMutex);
            m_encoderName = encoder.name();
            m_width = encoder.width();
            m_height = encoder.height();
            keyframe = true;
        }
        const auto ts90k = static_cast<std::uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count() * 9 / 100);
        std::vector<std::uint8_t> au;
        bool isKey = false;
        if (!encoder.encode(frame.data(), width * 4, ts90k, keyframe, au, isKey))
            continue;
        lastEncode = Clock::now();
        if (isKey)
            lastKeyframe = lastEncode;
        if (!au.empty())
            sendFrame(au, isKey, ts90k);
    }
}

void ScreenShare::sendFrame(const std::vector<std::uint8_t>& au, bool keyframe, std::uint32_t ts90k)
{
    // Pace bursts (keyframes) so they fit socket buffers and home routers:
    // at most max(20 Mbit/s, 4x the stream bitrate).
    const double bitsPerSecond = std::max(20e6, 4.0 * m_settings.bitrateKbps * 1000.0);
    std::uint64_t drops = 0;
    for (const auto& part : media::fragmentFrame(++m_frameNumber, au)) {
        const auto now = Clock::now();
        if (m_paceNext > now + std::chrono::milliseconds(2))
            std::this_thread::sleep_until(m_paceNext);
        m_paceNext = std::max(m_paceNext, now)
            + std::chrono::nanoseconds(static_cast<std::int64_t>(part.size() * 8 * 1e9 / bitsPerSecond));
        bool sent = false;
        for (int attempt = 0; attempt < 20 && !sent; ++attempt) {
            sent = m_voice.sendVideo(ts90k, part, keyframe);
            if (!sent)
                std::this_thread::sleep_for(std::chrono::milliseconds(1)); // socket buffer full
        }
        drops += sent ? 0 : 1;
    }
    std::lock_guard stats(m_statsMutex);
    ++m_frames;
    m_keyframes += keyframe ? 1 : 0;
    m_bytes += au.size();
    m_sendDrops += drops;
}

QJsonObject ScreenShare::statsJson() const
{
    std::lock_guard stats(m_statsMutex);
    return {{"source", m_source ? m_source->name() : QString()}, {"encoder", m_encoderName}, {"width", m_width},
        {"height", m_height}, {"fps", m_settings.fps}, {"bitrate_kbps", m_settings.bitrateKbps},
        {"frames", static_cast<double>(m_frames)}, {"keyframes", static_cast<double>(m_keyframes)},
        {"bytes", static_cast<double>(m_bytes)}, {"send_drops", static_cast<double>(m_sendDrops)}};
}

// ============================================================= StreamViewer

StreamViewer::StreamViewer(std::uint64_t userId, QString framePath)
    : m_userId(userId)
    , m_path(std::move(framePath))
{
}

StreamViewer::~StreamViewer()
{
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_one();
    if (m_decoder_thread.joinable())
        m_decoder_thread.join();
    m_writer.close();
}

bool StreamViewer::open(QString* error)
{
    if (!m_writer.create(m_path, error) || !m_decoder.open(error))
        return false;
    m_decoder_thread = std::thread([this] { decodeLoop(); });
    return true;
}

void StreamViewer::onPacket(const media::Header& header, std::span<const std::uint8_t> payload)
{
    m_sourceStream = header.streamId;
    auto frame = m_assembler.add(header, payload);
    if (!frame)
        return;
    ++m_frames;
    std::lock_guard lock(m_mutex);
    if (m_queue.size() >= kMaxQueuedFrames) {
        // The decoder fell behind: skip ahead to the next keyframe instead
        // of showing ever older pictures.
        m_queue.clear();
        m_assembler.reset();
        m_decodeFailed = true;
        if (!frame->keyframe)
            return;
    }
    m_queue.push_back(std::move(*frame));
    m_wake.notify_one();
}

bool StreamViewer::wantsKeyframe()
{
    if (!m_sourceStream || !(m_assembler.needKeyframe() || m_decodeFailed))
        return false;
    const std::int64_t now = nowMs();
    if (now - m_lastKeyframeRequestMs < kKeyframeRetryMs)
        return false;
    m_lastKeyframeRequestMs = now;
    m_decodeFailed = false;
    return true;
}

void StreamViewer::decodeLoop()
{
    for (;;) {
        media::FrameAssembler::Frame frame;
        {
            std::unique_lock lock(m_mutex);
            m_wake.wait(lock, [&] { return m_stop || !m_queue.empty(); });
            if (m_stop)
                return;
            frame = std::move(m_queue.front());
            m_queue.pop_front();
        }
        const bool ok = m_decoder.decode(frame.data, [&](const std::uint8_t* bgra, int w, int h, int stride) {
            if (m_writer.write(bgra, static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h),
                    static_cast<std::uint32_t>(stride))) {
                m_decoded.fetch_add(1, std::memory_order_relaxed);
                m_width = w;
                m_height = h;
            }
        });
        if (!ok)
            m_decodeFailed = true;
    }
}

QJsonObject StreamViewer::statsJson() const
{
    return {{"user_id", QString::number(m_userId)}, {"path", m_path}, {"width", m_width.load()},
        {"height", m_height.load()}, {"frames", static_cast<double>(m_frames)},
        {"decoded", static_cast<double>(m_decoded.load())}, {"lost", static_cast<double>(m_assembler.lostFrames())}};
}

// ============================================================= VideoManager

VideoManager::VideoManager(voice::VoiceEngine& voice, QObject* parent)
    : QObject(parent)
    , m_voice(voice)
{
    m_voice.setVideoHandler([this](const media::Header& h, std::span<const std::uint8_t> p) { onVideoPacket(h, p); });
    // Retries keyframe requests when a stream went quiet after a loss.
    m_keyframeTimer.setInterval(static_cast<int>(kKeyframeRetryMs));
    connect(&m_keyframeTimer, &QTimer::timeout, this, [this] {
        for (auto& [id, v] : m_viewers) {
            if (v->wantsKeyframe())
                m_voice.requestKeyframe(v->sourceStream());
        }
    });
}

VideoManager::~VideoManager()
{
    stopAll();
    m_voice.setVideoHandler({});
}

void VideoManager::onVideoPacket(const media::Header& h, std::span<const std::uint8_t> payload)
{
    if (h.type == media::PacketType::Control) {
        if (m_share)
            m_share->requestKeyframe();
        return;
    }
    auto it = m_viewers.find(h.senderId);
    if (it == m_viewers.end())
        return;
    it->second->onPacket(h, payload);
    if (it->second->wantsKeyframe())
        m_voice.requestKeyframe(it->second->sourceStream());
}

void VideoManager::startSharing(std::function<void(bool ok, const QString& error)> done)
{
    if (m_share) {
        done(false, QStringLiteral("already sharing"));
        return;
    }
    auto source = m_factory ? m_factory() : makePortalSource();
    m_share = std::make_unique<ScreenShare>(std::move(source), m_settings, m_voice);
    auto once = std::make_shared<std::function<void(bool, const QString&)>>(std::move(done));
    auto finish = [once](bool ok, const QString& error) {
        if (*once)
            std::exchange(*once, {})(ok, error);
    };
    connect(m_share.get(), &ScreenShare::started, this, [this, finish] {
        m_shareLive = true;
        emit changed();
        finish(true, {});
    });
    connect(m_share.get(), &ScreenShare::failed, this, [this, finish](const QString& reason) {
        // Deferred: the share is still on the stack of this signal.
        QTimer::singleShot(0, this, [this] {
            m_share.reset();
            m_shareLive = false;
        });
        finish(false, reason);
    });
    connect(m_share.get(), &ScreenShare::ended, this, [this, finish] {
        const bool wasLive = m_shareLive;
        QTimer::singleShot(0, this, [this] { stopSharing(); });
        finish(false, QStringLiteral("screen capture ended"));
        if (wasLive)
            emit sharingEnded();
    });
    m_share->start();
}

void VideoManager::stopSharing()
{
    if (!m_share)
        return;
    m_share.reset();
    m_shareLive = false;
    emit changed();
}

std::optional<QString> VideoManager::watch(std::uint64_t userId, QString* error)
{
    if (auto it = m_viewers.find(userId); it != m_viewers.end())
        return it->second->path();
    if (!paths::ensurePrivateDir(m_frameDir)) {
        if (error)
            *error = QStringLiteral("cannot create %1").arg(m_frameDir);
        return std::nullopt;
    }
    const QString path = QStringLiteral("%1/%2-%3.frame")
                             .arg(m_frameDir)
                             .arg(userId)
                             .arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));
    auto viewer = std::make_unique<StreamViewer>(userId, path);
    if (!viewer->open(error))
        return std::nullopt;
    m_viewers.emplace(userId, std::move(viewer));
    m_keyframeTimer.start();
    emit changed();
    return path;
}

void VideoManager::unwatch(std::uint64_t userId)
{
    if (m_viewers.erase(userId) == 0)
        return;
    if (m_viewers.empty())
        m_keyframeTimer.stop();
    emit changed();
}

void VideoManager::stopAll()
{
    stopSharing();
    if (!m_viewers.empty()) {
        m_viewers.clear();
        m_keyframeTimer.stop();
        emit changed();
    }
}

QJsonArray VideoManager::watchingJson() const
{
    QJsonArray out;
    for (const auto& [id, v] : m_viewers)
        out.append(QJsonObject{{"user_id", QString::number(id)}, {"path", v->path()}});
    return out;
}

QJsonObject VideoManager::statsJson() const
{
    QJsonArray viewers;
    for (const auto& [id, v] : m_viewers)
        viewers.append(v->statsJson());
    return {{"sharing", sharing()}, {"share", m_share ? m_share->statsJson() : QJsonObject()}, {"watching", viewers},
        {"encoders", QJsonArray::fromStringList(H264Encoder::available())}};
}

} // namespace omachat::video
