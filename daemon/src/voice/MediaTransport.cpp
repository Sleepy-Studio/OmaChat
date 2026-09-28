#include "voice/MediaTransport.hpp"

#include "omachat/core/Log.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <unistd.h>

namespace omachat::voice {

using namespace omachat::media;

MediaTransport::MediaTransport(QObject* parent)
    : QObject(parent)
{
    // Register repeatedly until the server acknowledges (UDP may drop it),
    // then keep NAT bindings alive at a low rate.
    m_registerTimer.setInterval(std::chrono::milliseconds(500));
    connect(&m_registerTimer, &QTimer::timeout, this, [this] { sendControl(ControlType::Register); });
    m_keepaliveTimer.setInterval(std::chrono::seconds(15));
    connect(&m_keepaliveTimer, &QTimer::timeout, this, [this] { sendControl(ControlType::Keepalive); });
}

MediaTransport::~MediaTransport()
{
    close();
}

bool MediaTransport::open(
    const QHostAddress& server, quint16 port, std::uint32_t streamId, const Key& key, QString* error)
{
    close();
    sockaddr_storage addr{};
    socklen_t len = 0;
    int family = AF_INET;
    if (server.protocol() == QAbstractSocket::IPv6Protocol && !server.toIPv4Address()) {
        family = AF_INET6;
        auto* a6 = reinterpret_cast<sockaddr_in6*>(&addr);
        a6->sin6_family = AF_INET6;
        a6->sin6_port = htons(port);
        const Q_IPV6ADDR raw = server.toIPv6Address();
        std::memcpy(&a6->sin6_addr, raw.c, 16);
        len = sizeof(sockaddr_in6);
    } else {
        bool ok = false;
        const quint32 v4 = server.toIPv4Address(&ok);
        if (!ok) {
            if (error)
                *error = QStringLiteral("unsupported server address");
            return false;
        }
        auto* a4 = reinterpret_cast<sockaddr_in*>(&addr);
        a4->sin_family = AF_INET;
        a4->sin_port = htons(port);
        a4->sin_addr.s_addr = htonl(v4);
        len = sizeof(sockaddr_in);
    }

    m_fd = ::socket(family, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (m_fd < 0) {
        if (error)
            *error = QString::fromLocal8Bit(std::strerror(errno));
        return false;
    }
    // Expedited Forwarding DSCP: lets QoS-aware networks prioritize voice.
    const int tos = 0xB8;
    if (family == AF_INET)
        ::setsockopt(m_fd, IPPROTO_IP, IP_TOS, &tos, sizeof tos);
    else
        ::setsockopt(m_fd, IPPROTO_IPV6, IPV6_TCLASS, &tos, sizeof tos);
    // connect() lets the kernel drop datagrams from any other source.
    if (::connect(m_fd, reinterpret_cast<sockaddr*>(&addr), len) != 0) {
        if (error)
            *error = QString::fromLocal8Bit(std::strerror(errno));
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    m_streamId = streamId;
    m_key = key;
    m_audioSeq = 0;
    m_videoSeq = 0;
    m_controlSeq = 0;
    m_registered = false;
    m_replay.clear();
    m_notifier = std::make_unique<QSocketNotifier>(m_fd, QSocketNotifier::Read);
    connect(m_notifier.get(), &QSocketNotifier::activated, this, &MediaTransport::onReadable);
    sendControl(ControlType::Register);
    m_registerTimer.start();
    m_keepaliveTimer.start();
    return true;
}

void MediaTransport::close()
{
    m_registerTimer.stop();
    m_keepaliveTimer.stop();
    m_notifier.reset();
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    if (m_registered.exchange(false))
        emit registeredChanged(false);
    m_key.fill(0);
}

bool MediaTransport::sendPacket(PacketType type, std::uint8_t flags, std::uint32_t timestamp,
    std::span<const std::uint8_t> payload, std::atomic<std::uint32_t>& seq)
{
    if (m_fd < 0)
        return false;
    Header h;
    h.type = type;
    h.flags = flags;
    h.streamId = m_streamId;
    h.sequence = seq.fetch_add(1, std::memory_order_relaxed) + 1;
    h.timestamp = timestamp;
    std::array<std::uint8_t, kMaxDatagramBytes> buf;
    const std::size_t n = sealInto(h, Direction::ClientToServer, m_key, payload, buf);
    if (n == 0)
        return false;
    const auto sent = ::send(m_fd, buf.data(), n, MSG_DONTWAIT);
    if (sent == static_cast<ssize_t>(n)) {
        m_sent.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    return false;
}

bool MediaTransport::sendAudio(std::uint32_t timestamp, std::span<const std::uint8_t> opus, bool endOfSpeech)
{
    return sendPacket(PacketType::Audio, endOfSpeech ? FlagEndOfSpeech : FlagNone, timestamp, opus, m_audioSeq);
}

bool MediaTransport::sendVideo(std::uint32_t timestamp, std::span<const std::uint8_t> payload, bool keyframe)
{
    return sendPacket(PacketType::Video, keyframe ? FlagKeyframe : FlagNone, timestamp, payload, m_videoSeq);
}

void MediaTransport::sendControl(ControlType type, std::span<const std::uint8_t> extra)
{
    std::array<std::uint8_t, 64> payload{};
    payload[0] = static_cast<std::uint8_t>(type);
    const std::size_t n = 1 + std::min<std::size_t>(extra.size(), payload.size() - 1);
    std::copy_n(extra.begin(), n - 1, payload.begin() + 1);
    sendPacket(PacketType::Control, 0, 0, std::span<const std::uint8_t>(payload.data(), n), m_controlSeq);
}

void MediaTransport::onReadable()
{
    for (;;) {
        const auto n = ::recv(m_fd, m_rx.data(), m_rx.size(), MSG_DONTWAIT);
        if (n <= 0)
            return;
        const std::span<const std::uint8_t> datagram(m_rx.data(), static_cast<std::size_t>(n));
        auto header = parseHeader(datagram);
        if (!header) {
            ++m_rejected;
            continue;
        }
        const auto len = media::open(*header, Direction::ServerToClient, m_key, datagram, m_plain);
        if (!len) {
            ++m_rejected;
            continue;
        }
        auto& window = m_replay[{header->streamId, static_cast<std::uint8_t>(header->type)}];
        if (!window.accept(header->sequence)) {
            ++m_rejected;
            continue;
        }
        ++m_received;
        const std::span<const std::uint8_t> plain(m_plain.data(), *len);
        if (header->type == PacketType::Control) {
            if (!plain.empty() && plain[0] == static_cast<std::uint8_t>(ControlType::RegisterAck)
                && !m_registered.exchange(true)) {
                m_registerTimer.stop();
                OMA_DEBUG("media", "udp registered", {"stream", m_streamId});
                emit registeredChanged(true);
            }
            if (m_receiver)
                m_receiver(*header, plain);
            continue;
        }
        if (m_receiver)
            m_receiver(*header, plain);
    }
}

MediaTransport::Stats MediaTransport::stats() const
{
    return Stats{m_sent.load(std::memory_order_relaxed), m_received, m_rejected};
}

} // namespace omachat::voice
