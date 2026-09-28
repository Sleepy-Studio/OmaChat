#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

struct OpusEncoder;
struct OpusDecoder;

namespace omachat::voice {

inline constexpr int kSampleRate = 48000;
inline constexpr int kChannels = 1;
inline constexpr int kFrameSamples = 960; // 20 ms
inline constexpr int kMaxPacketBytes = 1275;

class OpusVoiceEncoder {
public:
    OpusVoiceEncoder();
    ~OpusVoiceEncoder();

    bool valid() const { return m_enc != nullptr; }
    void setBitrate(int bps);
    void setFec(bool enabled, int expectedLossPercent);

    // Encodes exactly kFrameSamples samples. Returns the packet size, or 0.
    std::size_t encode(std::span<const float> pcm, std::span<std::uint8_t> out);

private:
    OpusEncoder* m_enc = nullptr;
};

class OpusVoiceDecoder {
public:
    OpusVoiceDecoder();
    ~OpusVoiceDecoder();
    OpusVoiceDecoder(const OpusVoiceDecoder&) = delete;
    OpusVoiceDecoder& operator=(const OpusVoiceDecoder&) = delete;

    bool valid() const { return m_dec != nullptr; }

    // Decodes one packet into kFrameSamples samples. Returns samples written.
    int decode(std::span<const std::uint8_t> packet, std::span<float> out);
    // Recovers the frame *before* `nextPacket` from its in-band FEC data.
    int decodeFec(std::span<const std::uint8_t> nextPacket, std::span<float> out);
    // Packet-loss concealment for one missing frame.
    int conceal(std::span<float> out);

private:
    OpusDecoder* m_dec = nullptr;
};

} // namespace omachat::voice
