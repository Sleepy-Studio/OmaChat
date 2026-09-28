#include "omachat/media/MediaPacket.hpp"

#include <sodium.h>

#include <cstring>

namespace omachat::media {
namespace {

void put32(std::uint8_t* p, std::uint32_t v)
{
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

void put64(std::uint8_t* p, std::uint64_t v)
{
    put32(p, static_cast<std::uint32_t>(v >> 32));
    put32(p + 4, static_cast<std::uint32_t>(v));
}

std::uint32_t get32(const std::uint8_t* p)
{
    return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) | std::uint32_t{p[3]};
}

std::uint64_t get64(const std::uint8_t* p)
{
    return (std::uint64_t{get32(p)} << 32) | get32(p + 4);
}

std::array<std::uint8_t, crypto_aead_chacha20poly1305_ietf_NPUBBYTES> nonceFor(const Header& h, Direction dir)
{
    static_assert(crypto_aead_chacha20poly1305_ietf_NPUBBYTES == 12);
    std::array<std::uint8_t, 12> n{};
    n[0] = static_cast<std::uint8_t>(dir);
    n[1] = static_cast<std::uint8_t>(h.type);
    put32(n.data() + 4, h.streamId);
    put32(n.data() + 8, h.sequence);
    return n;
}

} // namespace

bool initializeCrypto()
{
    return sodium_init() >= 0;
}

void writeHeader(const Header& h, std::span<std::uint8_t, kHeaderBytes> out)
{
    out[0] = h.version;
    out[1] = static_cast<std::uint8_t>(h.type);
    out[2] = h.flags;
    out[3] = 0;
    put32(out.data() + 4, h.streamId);
    put64(out.data() + 8, h.senderId);
    put32(out.data() + 16, h.sequence);
    put32(out.data() + 20, h.timestamp);
}

std::optional<Header> parseHeader(std::span<const std::uint8_t> d)
{
    if (d.size() < kHeaderBytes + kTagBytes || d.size() > 65507)
        return std::nullopt;
    if (d[0] != kMediaVersion || d[3] != 0)
        return std::nullopt;
    if (d[1] < static_cast<std::uint8_t>(PacketType::Audio) || d[1] > static_cast<std::uint8_t>(PacketType::Control))
        return std::nullopt;
    Header h;
    h.version = d[0];
    h.type = static_cast<PacketType>(d[1]);
    h.flags = d[2];
    h.streamId = get32(d.data() + 4);
    h.senderId = get64(d.data() + 8);
    h.sequence = get32(d.data() + 16);
    h.timestamp = get32(d.data() + 20);
    return h;
}

std::size_t sealInto(
    const Header& h, Direction dir, const Key& key, std::span<const std::uint8_t> payload, std::span<std::uint8_t> out)
{
    if (payload.size() > kMaxPayloadBytes || out.size() < kHeaderBytes + payload.size() + kTagBytes)
        return 0;
    writeHeader(h, out.first<kHeaderBytes>());
    const auto nonce = nonceFor(h, dir);
    unsigned long long clen = 0;
    if (crypto_aead_chacha20poly1305_ietf_encrypt(out.data() + kHeaderBytes, &clen, payload.data(), payload.size(),
            out.data(), kHeaderBytes, nullptr, nonce.data(), key.data())
        != 0)
        return 0;
    return kHeaderBytes + static_cast<std::size_t>(clen);
}

std::vector<std::uint8_t> seal(const Header& h, Direction dir, const Key& key, std::span<const std::uint8_t> payload)
{
    std::vector<std::uint8_t> out(kHeaderBytes + payload.size() + kTagBytes);
    const std::size_t n = sealInto(h, dir, key, payload, out);
    out.resize(n);
    return out;
}

std::optional<std::size_t> open(
    const Header& h, Direction dir, const Key& key, std::span<const std::uint8_t> datagram, std::span<std::uint8_t> out)
{
    if (datagram.size() < kHeaderBytes + kTagBytes)
        return std::nullopt;
    const std::size_t plainLen = datagram.size() - kHeaderBytes - kTagBytes;
    if (out.size() < plainLen)
        return std::nullopt;

    // Re-serialize the header that the sender authenticated. The server
    // rewrites sender_id when forwarding and re-seals, so the header parsed
    // from this datagram is exactly what was authenticated.
    std::array<std::uint8_t, kHeaderBytes> aad{};
    writeHeader(h, aad);
    if (std::memcmp(aad.data(), datagram.data(), kHeaderBytes) != 0)
        return std::nullopt;

    const auto nonce = nonceFor(h, dir);
    unsigned long long mlen = 0;
    if (crypto_aead_chacha20poly1305_ietf_decrypt(out.data(), &mlen, nullptr, datagram.data() + kHeaderBytes,
            datagram.size() - kHeaderBytes, aad.data(), kHeaderBytes, nonce.data(), key.data())
        != 0)
        return std::nullopt;
    return static_cast<std::size_t>(mlen);
}

bool ReplayWindow::accept(std::uint32_t seq)
{
    if (!m_initialized) {
        m_initialized = true;
        m_highest = seq;
        m_bitmap = 1;
        return true;
    }
    // Serial-number arithmetic handles 32-bit wraparound.
    const auto delta = static_cast<std::int32_t>(seq - m_highest);
    if (delta > 0) {
        m_bitmap = delta >= 64 ? 1 : (m_bitmap << delta) | 1;
        m_highest = seq;
        return true;
    }
    const auto behind = static_cast<std::uint32_t>(-delta);
    if (behind >= 64)
        return false;
    const std::uint64_t mask = std::uint64_t{1} << behind;
    if (m_bitmap & mask)
        return false;
    m_bitmap |= mask;
    return true;
}

Key randomKey()
{
    Key k{};
    randombytes_buf(k.data(), k.size());
    return k;
}

} // namespace omachat::media
