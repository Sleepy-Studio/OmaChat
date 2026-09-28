#pragma once

#include "omachat/media/MediaPacket.hpp"

#include <QHostAddress>
#include <QObject>
#include <QSocketNotifier>
#include <QTimer>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <span>

namespace omachat::voice {

// Client side of the encrypted UDP media channel. Receiving happens on the
// Qt thread; send*() is safe to call from the encoder thread.
class MediaTransport : public QObject {
    Q_OBJECT
public:
    using Receiver = std::function<void(const media::Header& header, std::span<const std::uint8_t> payload)>;

    explicit MediaTransport(QObject* parent = nullptr);
    ~MediaTransport() override;

    bool open(const QHostAddress& server, quint16 port, std::uint32_t streamId, const media::Key& key, QString* error);
    void close();
    bool isOpen() const { return m_fd >= 0; }
    bool registered() const { return m_registered.load(std::memory_order_relaxed); }
    std::uint32_t streamId() const { return m_streamId; }

    void setReceiver(Receiver r) { m_receiver = std::move(r); }

    bool sendAudio(std::uint32_t timestamp, std::span<const std::uint8_t> opus, bool endOfSpeech);
    bool sendVideo(std::uint32_t timestamp, std::span<const std::uint8_t> payload, bool keyframe);
    void sendControl(media::ControlType type, std::span<const std::uint8_t> extra = {});

    struct Stats {
        std::uint64_t sent = 0;
        std::uint64_t received = 0;
        std::uint64_t rejected = 0;
    };
    Stats stats() const;

signals:
    void registeredChanged(bool registered);

private:
    bool sendPacket(media::PacketType type, std::uint8_t flags, std::uint32_t timestamp,
        std::span<const std::uint8_t> payload, std::atomic<std::uint32_t>& seq);
    void onReadable();

    int m_fd = -1;
    std::unique_ptr<QSocketNotifier> m_notifier;
    std::uint32_t m_streamId = 0;
    media::Key m_key{};
    std::atomic<std::uint32_t> m_audioSeq{0};
    std::atomic<std::uint32_t> m_videoSeq{0};
    std::atomic<std::uint32_t> m_controlSeq{0};
    std::atomic<bool> m_registered{false};
    std::atomic<std::uint64_t> m_sent{0};
    std::uint64_t m_received = 0;
    std::uint64_t m_rejected = 0;

    std::map<std::pair<std::uint32_t, std::uint8_t>, media::ReplayWindow> m_replay;
    Receiver m_receiver;
    QTimer m_registerTimer;
    QTimer m_keepaliveTimer;
    std::array<std::uint8_t, 2048> m_rx{};
    std::array<std::uint8_t, 2048> m_plain{};
};

} // namespace omachat::voice
