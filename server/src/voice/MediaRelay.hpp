#pragma once

#include "omachat/core/RateLimiter.hpp"
#include "omachat/media/MediaPacket.hpp"

#include <QHostAddress>
#include <QObject>
#include <QUdpSocket>

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace omachat::server {

// SFU-style UDP forwarder. Authenticates every datagram with the sender's
// session key, then re-seals it for each eligible recipient in the same
// voice channel. Opus payloads are never decoded.
class MediaRelay : public QObject {
    Q_OBJECT
public:
    explicit MediaRelay(QObject* parent = nullptr);

    bool bind(const QHostAddress& address, quint16 port, QString* error);
    quint16 port() const { return m_socket.localPort(); }

    struct StreamInfo {
        std::uint32_t streamId = 0;
        std::uint64_t userId = 0;
        std::uint64_t channelId = 0;
        media::Key key{};
        std::int64_t expiresAtMs = 0;
    };

    void addStream(const StreamInfo& info);
    void removeStream(std::uint32_t streamId);

    // canSpeak: the server lets this stream's audio through (not muted, has SPEAK).
    // deaf: the stream's owner receives no audio.
    void setStreamFlags(std::uint32_t streamId, bool canSpeak, bool deaf);

    // Video subscriptions: only viewers receive a streamer's video packets.
    void setSubscription(std::uint32_t viewerStream, std::uint32_t sourceStream, bool subscribed);
    void setVideoAllowed(std::uint32_t streamId, bool allowed);

    std::uint32_t allocateStreamId();

    struct Stats {
        std::uint64_t received = 0;
        std::uint64_t forwarded = 0;
        std::uint64_t droppedUnknown = 0;
        std::uint64_t droppedAuth = 0;
        std::uint64_t droppedReplay = 0;
        std::uint64_t droppedRate = 0;
    };
    const Stats& stats() const { return m_stats; }

signals:
    // Emitted when a stream's UDP endpoint becomes known (voice is live).
    void streamRegistered(std::uint32_t streamId);
    void keyframeRequested(std::uint32_t sourceStream);

private:
    struct Stream {
        StreamInfo info;
        QHostAddress address;
        quint16 port = 0;
        bool registered = false;
        bool canSpeak = true;
        bool deaf = false;
        bool videoAllowed = false;
        std::array<media::ReplayWindow, 4> replay{};
        TokenBucket audioBucket{400, 200}; // 50 pps nominal; generous headroom
        TokenBucket videoBucket{4000, 2500}; // fragmented 1080p60 at ~8 Mbps
        TokenBucket controlBucket{20, 5};
        std::uint32_t controlSeq = 0;
        std::set<std::uint32_t> viewers; // streams subscribed to this stream's video
    };

    void onReadyRead();
    void handleDatagram(std::span<const std::uint8_t> datagram, const QHostAddress& from, quint16 port);
    void forward(const Stream& source, const media::Header& header, std::span<const std::uint8_t> plain);
    void sendControl(Stream& target, media::ControlType type);

    QUdpSocket m_socket;
    std::map<std::uint32_t, Stream> m_streams;
    std::map<std::uint64_t, std::vector<std::uint32_t>> m_channelStreams;
    std::uint32_t m_nextStreamId = 1;
    Stats m_stats;

    std::array<std::uint8_t, 65536> m_rx{};
    std::array<std::uint8_t, media::kMaxDatagramBytes> m_plain{};
    std::array<std::uint8_t, media::kMaxDatagramBytes> m_tx{};
};

} // namespace omachat::server
