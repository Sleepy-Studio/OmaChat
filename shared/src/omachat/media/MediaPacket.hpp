#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace omachat::media {

// Real-time UDP packet layout (all integers network byte order):
//
//   off size field
//   0   1    version        (kMediaVersion)
//   1   1    type           (PacketType)
//   2   1    flags          (PacketFlags)
//   3   1    reserved       (must be 0)
//   4   4    stream_id      originating stream (assigned by the server)
//   8   8    sender_id      user id; 0 on client->server, filled by server
//   16  4    sequence       per (stream, type) counter
//   20  4    timestamp      48 kHz sample clock for audio, 90 kHz for video
//   24  n    ciphertext     ChaCha20-Poly1305-IETF(payload), 16-byte tag appended
//
// The 24-byte header is authenticated as associated data. The 12-byte nonce is
//   [direction][type][0][0][stream_id:4][sequence:4]
// which is unique per key because each key belongs to exactly one client
// session, directions never share nonces, and each (stream, type) sequence is
// strictly increasing.
inline constexpr std::uint8_t kMediaVersion = 1;
inline constexpr std::size_t kHeaderBytes = 24;
inline constexpr std::size_t kTagBytes = 16;
inline constexpr std::size_t kKeyBytes = 32;
inline constexpr std::size_t kMaxDatagramBytes = 1400; // stay under common path MTU
inline constexpr std::size_t kMaxPayloadBytes = kMaxDatagramBytes - kHeaderBytes - kTagBytes;

enum class PacketType : std::uint8_t {
    Audio = 1,
    Video = 2,
    Control = 3,
};

enum PacketFlags : std::uint8_t {
    FlagNone = 0,
    FlagEndOfSpeech = 1 << 0, // final audio frame of a talk spurt
    FlagKeyframe = 1 << 1, // video: fragment belongs to a keyframe
    FlagScreenAudio = 1 << 2, // video stream: the payload is screen-share audio, not a fragment
    FlagPointer = 1 << 3, // video stream: the payload is an ephemeral pointer position, not a fragment
};

enum class Direction : std::uint8_t {
    ClientToServer = 1,
    ServerToClient = 2,
};

// Control packet subtypes (first payload byte of PacketType::Control).
enum class ControlType : std::uint8_t {
    Register = 1, // client -> server: bind this UDP endpoint to the stream
    Keepalive = 2, // client -> server: keep NAT bindings alive
    RegisterAck = 3,
    KeyframeRequest = 4, // viewer -> server -> streamer
};

using Key = std::array<std::uint8_t, kKeyBytes>;

struct Header {
    std::uint8_t version = kMediaVersion;
    PacketType type = PacketType::Audio;
    std::uint8_t flags = 0;
    std::uint32_t streamId = 0;
    std::uint64_t senderId = 0;
    std::uint32_t sequence = 0;
    std::uint32_t timestamp = 0;
};

void writeHeader(const Header& h, std::span<std::uint8_t, kHeaderBytes> out);

// Parses only the cleartext header. Returns nullopt for short packets,
// unknown versions/types or non-zero reserved bytes.
std::optional<Header> parseHeader(std::span<const std::uint8_t> datagram);

// Builds a complete datagram. Returns empty on oversize payloads.
std::vector<std::uint8_t> seal(const Header& h, Direction dir, const Key& key, std::span<const std::uint8_t> payload);

// Same as seal() but writes into caller storage (no allocation). Returns the
// datagram size, or 0 on failure. `out` must hold kMaxDatagramBytes.
std::size_t sealInto(
    const Header& h, Direction dir, const Key& key, std::span<const std::uint8_t> payload, std::span<std::uint8_t> out);

// Authenticates and decrypts. Returns the plaintext length written to `out`
// (which must be at least datagram.size() - kHeaderBytes - kTagBytes bytes),
// or nullopt if authentication fails.
std::optional<std::size_t> open(const Header& h, Direction dir, const Key& key, std::span<const std::uint8_t> datagram,
    std::span<std::uint8_t> out);

// Sliding-window replay filter (64 packets) for one (stream, type).
class ReplayWindow {
public:
    // Returns true when `sequence` is new and records it.
    bool accept(std::uint32_t sequence);
    void reset()
    {
        m_initialized = false;
        m_bitmap = 0;
        m_highest = 0;
    }

private:
    bool m_initialized = false;
    std::uint32_t m_highest = 0;
    std::uint64_t m_bitmap = 0;
};

Key randomKey();

// libsodium must be initialized once per process before seal/open.
bool initializeCrypto();

} // namespace omachat::media
