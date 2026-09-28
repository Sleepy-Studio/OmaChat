#include "FuzzTargets.hpp"

#include "crypto/E2E.hpp"
#include "network.pb.h"
#include "networking/ClientState.hpp"
#include "omachat/ipc/IpcMessage.hpp"
#include "omachat/media/MediaPacket.hpp"
#include "omachat/media/VideoFragments.hpp"
#include "omachat/protocol/Framing.hpp"
#include "voice/JitterBuffer.hpp"

#include <array>

namespace omachat::fuzz {

void framing(const std::uint8_t* data, std::size_t size)
{
    protocol::FrameDecoder dec(64 * 1024);
    // Feed in uneven chunks to exercise reassembly.
    std::size_t pos = 0, step = 1;
    while (pos < size) {
        const std::size_t n = std::min(step, size - pos);
        dec.feed(QByteArray(reinterpret_cast<const char*>(data + pos), static_cast<qsizetype>(n)));
        pos += n;
        step = step * 3 % 97 + 1;
        QByteArray frame;
        for (;;) {
            const auto st = dec.next(frame);
            if (st != protocol::FrameDecoder::Status::Frame)
                break;
            proto::Envelope env;
            (void)env.ParseFromArray(frame.constData(), static_cast<int>(frame.size()));
        }
    }
}

void mediaHeader(const std::uint8_t* data, std::size_t size)
{
    const std::span<const std::uint8_t> d(data, size);
    const auto h = media::parseHeader(d);
    if (!h)
        return;
    static const media::Key key = [] {
        media::Key k{};
        for (std::size_t i = 0; i < k.size(); ++i)
            k[i] = static_cast<std::uint8_t>(i * 7 + 1);
        return k;
    }();
    std::array<std::uint8_t, 70000> out{};
    media::open(*h, media::Direction::ServerToClient, key, d, out);
    media::ReplayWindow w;
    w.accept(h->sequence);
    // Push the (unauthenticated) bytes through a jitter buffer as a worst case.
    voice::JitterBuffer jb;
    jb.push(h->sequence, h->timestamp, d.subspan(std::min<std::size_t>(size, media::kHeaderBytes)), h->flags & 1);
    voice::JitterBuffer::Slot slot;
    jb.pop(slot);
}

void envelope(const std::uint8_t* data, std::size_t size)
{
    proto::Envelope env;
    if (!env.ParseFromArray(data, static_cast<int>(size)))
        return;
    // Events from a (malicious) server must never crash the daemon model.
    daemon::ClientState state;
    if (env.has_sync_state())
        state.reset(env.sync_state());
    if (env.has_event()) {
        std::vector<daemon::ModelEvent> out;
        bool resync = false;
        state.apply(env.event(), out, resync);
        state.snapshotJson();
    }
    if (env.has_chat_message())
        state.messageJson(env.chat_message());
}

void ipcLine(const std::uint8_t* data, std::size_t size)
{
    ipc::LineDecoder dec;
    dec.feed(QByteArray(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size)));
    QByteArray line;
    while (dec.next(line) == ipc::LineDecoder::Status::Line) {
        if (auto obj = ipc::parse(line)) {
            daemon::idFromJson(obj->value(QStringLiteral("id")));
            daemon::idFromJson(obj->value(QStringLiteral("params")).toObject().value(QStringLiteral("channel")));
        }
    }
}

void videoFragments(const std::uint8_t* data, std::size_t size)
{
    media::FrameAssembler assembler;
    std::size_t pos = 0;
    while (pos + 3 <= size) {
        media::Header h;
        h.type = media::PacketType::Video;
        h.flags = data[pos];
        const std::size_t len
            = std::min<std::size_t>((std::size_t{data[pos + 1]} << 8) | data[pos + 2], size - pos - 3);
        pos += 3;
        if (auto frame = assembler.add(h, std::span<const std::uint8_t>(data + pos, len)))
            (void)frame->data.size();
        pos += len;
    }
}

void e2ePayload(const std::uint8_t* data, std::size_t size)
{
    static const e2e::Identity me = e2e::Identity::generate();
    const std::string payload(reinterpret_cast<const char*>(data), size);
    if (auto opened = e2e::open(payload, {1, 2}, me))
        (void)opened->body.content().size();
}

} // namespace omachat::fuzz
