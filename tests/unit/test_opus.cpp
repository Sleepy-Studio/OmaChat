#include "voice/OpusCodec.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <numbers>
#include <vector>

using namespace omachat::voice;

namespace {

std::vector<float> sine(int frames, double hz, double amplitude = 0.3)
{
    std::vector<float> out(static_cast<size_t>(frames * kFrameSamples));
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<float>(
            amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kSampleRate));
    return out;
}

double rms(std::span<const float> x)
{
    double e = 0;
    for (float v : x)
        e += static_cast<double>(v) * v;
    return std::sqrt(e / static_cast<double>(x.size()));
}

} // namespace

TEST(Opus, EncodeDecodePreservesSignal)
{
    OpusVoiceEncoder enc;
    OpusVoiceDecoder dec;
    ASSERT_TRUE(enc.valid());
    ASSERT_TRUE(dec.valid());
    enc.setBitrate(40000);
    const auto input = sine(50, 440.0);
    std::array<std::uint8_t, kMaxPacketBytes> packet{};
    std::array<float, kFrameSamples> out{};
    double outEnergy = 0;
    std::size_t totalBytes = 0;
    for (int f = 0; f < 50; ++f) {
        const auto n = enc.encode(
            std::span<const float>(input).subspan(static_cast<size_t>(f * kFrameSamples), kFrameSamples), packet);
        ASSERT_GT(n, 0u);
        totalBytes += n;
        ASSERT_EQ(dec.decode(std::span<const std::uint8_t>(packet.data(), n), out), kFrameSamples);
        if (f > 5)
            outEnergy += rms(out);
    }
    EXPECT_NEAR(outEnergy / 44.0, 0.3 / std::numbers::sqrt2, 0.05);
    // ~40 kbps => ~100 bytes per 20 ms frame.
    EXPECT_LT(totalBytes / 50, 160u);
}

TEST(Opus, ConcealmentAndFecProduceFullFrames)
{
    OpusVoiceEncoder enc;
    OpusVoiceDecoder dec;
    enc.setFec(true, 10);
    const auto input = sine(3, 300.0);
    std::array<std::uint8_t, kMaxPacketBytes> p1{}, p2{}, p3{};
    const auto n1 = enc.encode(std::span<const float>(input).subspan(0, kFrameSamples), p1);
    const auto n2 = enc.encode(std::span<const float>(input).subspan(kFrameSamples, kFrameSamples), p2);
    const auto n3 = enc.encode(std::span<const float>(input).subspan(2 * kFrameSamples, kFrameSamples), p3);
    ASSERT_TRUE(n1 && n2 && n3);
    std::array<float, kFrameSamples> out{};
    EXPECT_EQ(dec.decode(std::span<const std::uint8_t>(p1.data(), n1), out), kFrameSamples);
    // Frame 2 lost: recover it from frame 3's in-band FEC, then decode 3.
    EXPECT_EQ(dec.decodeFec(std::span<const std::uint8_t>(p3.data(), n3), out), kFrameSamples);
    EXPECT_EQ(dec.decode(std::span<const std::uint8_t>(p3.data(), n3), out), kFrameSamples);
    EXPECT_EQ(dec.conceal(out), kFrameSamples);
}

TEST(Opus, RejectsShortInput)
{
    OpusVoiceEncoder enc;
    std::array<float, 100> tooShort{};
    std::array<std::uint8_t, kMaxPacketBytes> packet{};
    EXPECT_EQ(enc.encode(tooShort, packet), 0u);
}

TEST(Opus, GarbagePacketDoesNotCrash)
{
    OpusVoiceDecoder dec;
    std::array<std::uint8_t, 7> junk{0xff, 0xfe, 0x01, 0x02, 0x03, 0x04, 0x05};
    std::array<float, kFrameSamples> out{};
    EXPECT_GE(dec.decode(junk, out), 0);
}
