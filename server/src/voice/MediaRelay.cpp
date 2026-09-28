#include "voice/MediaRelay.hpp"

#include "omachat/core/Log.hpp"

#include <QDateTime>

#include <algorithm>

namespace omachat::server {

using namespace omachat::media;

MediaRelay::MediaRelay(QObject* parent)
    : QObject(parent)
{
    connect(&m_socket, &QUdpSocket::readyRead, this, &MediaRelay::onReadyRead);
}

bool MediaRelay::bind(const QHostAddress& address, quint16 port, QString* error)
{
    if (!m_socket.bind(address, port)) {
        if (error)
            *error = m_socket.errorString();
        return false;
    }
    // Voice bursts from many clients arrive together; give the kernel room.
    // Screen-share keyframes arrive and leave as bursts of hundreds of datagrams.
    m_socket.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 4 << 20);
    m_socket.setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 4 << 20);
    return true;
}

std::uint32_t MediaRelay::allocateStreamId()
{
    // Stream ids are never reused within a process lifetime, which keeps
    // AEAD nonces unique for every per-session key.
    return m_nextStreamId++;
}

void MediaRelay::addStream(const StreamInfo& info)
{
    Stream s;
    s.info = info;
    m_streams[info.streamId] = std::move(s);
    m_channelStreams[info.channelId].push_back(info.streamId);
}

void MediaRelay::removeStream(std::uint32_t streamId)
{
    auto it = m_streams.find(streamId);
    if (it == m_streams.end())
        return;
    auto& list = m_channelStreams[it->second.info.channelId];
    std::erase(list, streamId);
    if (list.empty())
        m_channelStreams.erase(it->second.info.channelId);
    for (auto& [id, other] : m_streams)
        other.viewers.erase(streamId);
    m_streams.erase(it);
}

void MediaRelay::setStreamFlags(std::uint32_t streamId, bool canSpeak, bool deaf)
{
    auto it = m_streams.find(streamId);
    if (it == m_streams.end())
        return;
    it->second.canSpeak = canSpeak;
    it->second.deaf = deaf;
}

void MediaRelay::setSubscription(std::uint32_t viewerStream, std::uint32_t sourceStream, bool subscribed)
{
    auto it = m_streams.find(sourceStream);
    if (it == m_streams.end())
        return;
    if (subscribed)
        it->second.viewers.insert(viewerStream);
    else
        it->second.viewers.erase(viewerStream);
}

void MediaRelay::setVideoAllowed(std::uint32_t streamId, bool allowed)
{
    auto it = m_streams.find(streamId);
    if (it != m_streams.end())
        it->second.videoAllowed = allowed;
}

void MediaRelay::clearViewers(std::uint32_t sourceStream)
{
    if (auto it = m_streams.find(sourceStream); it != m_streams.end())
        it->second.viewers.clear();
}

void MediaRelay::requestKeyframe(std::uint32_t sourceStream)
{
    if (auto it = m_streams.find(sourceStream); it != m_streams.end())
        sendControl(it->second, ControlType::KeyframeRequest);
}

void MediaRelay::onReadyRead()
{
    while (m_socket.hasPendingDatagrams()) {
        QHostAddress from;
        quint16 port = 0;
        const qint64 n = m_socket.readDatagram(
            reinterpret_cast<char*>(m_rx.data()), static_cast<qint64>(m_rx.size()), &from, &port);
        if (n <= 0)
            continue;
        ++m_stats.received;
        handleDatagram(std::span<const std::uint8_t>(m_rx.data(), static_cast<size_t>(n)), from, port);
    }
}

void MediaRelay::handleDatagram(std::span<const std::uint8_t> datagram, const QHostAddress& from, quint16 port)
{
    auto header = parseHeader(datagram);
    if (!header || datagram.size() > kMaxDatagramBytes) {
        ++m_stats.droppedUnknown;
        return;
    }
    auto it = m_streams.find(header->streamId);
    if (it == m_streams.end() || header->senderId != 0) {
        ++m_stats.droppedUnknown;
        return;
    }
    Stream& s = it->second;
    if (QDateTime::currentMSecsSinceEpoch() > s.info.expiresAtMs) {
        ++m_stats.droppedUnknown;
        return;
    }

    // Rate-limit before paying for decryption.
    TokenBucket& bucket = header->type == PacketType::Audio ? s.audioBucket
        : header->type == PacketType::Video                 ? s.videoBucket
                                                            : s.controlBucket;
    if (!bucket.tryConsume()) {
        ++m_stats.droppedRate;
        return;
    }

    const auto plainLen = open(*header, Direction::ClientToServer, s.info.key, datagram, m_plain);
    if (!plainLen) {
        ++m_stats.droppedAuth;
        return;
    }
    auto& window = s.replay[static_cast<size_t>(header->type)];
    if (!window.accept(header->sequence)) {
        ++m_stats.droppedReplay;
        return;
    }
    const std::span<const std::uint8_t> plain(m_plain.data(), *plainLen);

    switch (header->type) {
    case PacketType::Control: {
        if (plain.empty())
            return;
        const auto type = static_cast<ControlType>(plain[0]);
        if (type == ControlType::Register || type == ControlType::Keepalive) {
            const bool wasRegistered = s.registered;
            s.address = from;
            s.port = port;
            s.registered = true;
            if (type == ControlType::Register)
                sendControl(s, ControlType::RegisterAck);
            if (!wasRegistered) {
                OMA_DEBUG("media", "stream registered", {"stream", s.info.streamId}, {"user", s.info.userId});
                emit streamRegistered(s.info.streamId);
            }
        } else if (type == ControlType::KeyframeRequest && plain.size() >= 5) {
            const std::uint32_t source = (std::uint32_t{plain[1]} << 24) | (std::uint32_t{plain[2]} << 16)
                | (std::uint32_t{plain[3]} << 8) | plain[4];
            auto src = m_streams.find(source);
            if (src != m_streams.end() && src->second.viewers.contains(s.info.streamId))
                sendControl(src->second, ControlType::KeyframeRequest);
        }
        return;
    }
    case PacketType::Audio:
        if (!s.registered || !s.canSpeak)
            return;
        forward(s, *header, plain);
        return;
    case PacketType::Video:
        if (!s.registered || !s.videoAllowed)
            return;
        forward(s, *header, plain);
        return;
    }
}

void MediaRelay::forward(const Stream& source, const Header& in, std::span<const std::uint8_t> plain)
{
    Header out = in;
    out.senderId = source.info.userId;

    auto sendTo = [&](const Stream& target) {
        const std::size_t len = sealInto(out, Direction::ServerToClient, target.info.key, plain, m_tx);
        if (len == 0)
            return;
        m_socket.writeDatagram(
            reinterpret_cast<const char*>(m_tx.data()), static_cast<qint64>(len), target.address, target.port);
        ++m_stats.forwarded;
    };

    if (in.type == PacketType::Video) {
        for (std::uint32_t viewer : source.viewers) {
            auto it = m_streams.find(viewer);
            if (it != m_streams.end() && it->second.registered)
                sendTo(it->second);
        }
        return;
    }

    auto cit = m_channelStreams.find(source.info.channelId);
    if (cit == m_channelStreams.end())
        return;
    for (std::uint32_t id : cit->second) {
        if (id == source.info.streamId)
            continue;
        auto it = m_streams.find(id);
        if (it == m_streams.end() || !it->second.registered || it->second.deaf)
            continue;
        sendTo(it->second);
    }
}

void MediaRelay::sendControl(Stream& target, ControlType type)
{
    if (!target.registered)
        return;
    Header h;
    h.type = PacketType::Control;
    h.streamId = target.info.streamId;
    h.sequence = ++target.controlSeq;
    const std::uint8_t payload[1] = {static_cast<std::uint8_t>(type)};
    const std::size_t len = sealInto(h, Direction::ServerToClient, target.info.key, payload, m_tx);
    if (len)
        m_socket.writeDatagram(
            reinterpret_cast<const char*>(m_tx.data()), static_cast<qint64>(len), target.address, target.port);
}

} // namespace omachat::server
