#include "voice/JitterBuffer.hpp"

#include <gtest/gtest.h>

#include <array>

using omachat::voice::JitterBuffer;
using Kind = JitterBuffer::Kind;
using namespace std::chrono_literals;

namespace {

struct Feeder {
    JitterBuffer& jb;
    JitterBuffer::Clock::time_point t0 = JitterBuffer::Clock::now();

    void push(std::uint32_t seq, std::chrono::milliseconds arrival, bool end = false)
    {
        std::array<std::uint8_t, 3> payload{static_cast<std::uint8_t>(seq), 0xAB, 0xCD};
        jb.push(seq, seq * 960, payload, end, t0 + arrival);
    }
};

Kind popKind(JitterBuffer& jb, std::uint8_t* firstByte = nullptr)
{
    JitterBuffer::Slot s;
    jb.pop(s);
    if (firstByte && s.size)
        *firstByte = s.data[0];
    return s.kind;
}

} // namespace

TEST(JitterBuffer, WaitsForTargetThenPlaysInOrder)
{
    JitterBuffer jb(40, 200); // min 2 frames
    Feeder f{jb};
    f.push(10, 0ms);
    EXPECT_EQ(popKind(jb), Kind::Silence) << "must buffer before starting";
    f.push(11, 20ms);
    std::uint8_t b = 0;
    EXPECT_EQ(popKind(jb, &b), Kind::Packet);
    EXPECT_EQ(b, 10);
    EXPECT_EQ(popKind(jb, &b), Kind::Packet);
    EXPECT_EQ(b, 11);
}

TEST(JitterBuffer, ReordersOutOfOrderArrivals)
{
    JitterBuffer jb(40, 200);
    Feeder f{jb};
    f.push(3, 0ms);
    f.push(1, 1ms);
    f.push(2, 2ms);
    std::uint8_t b = 0;
    ASSERT_EQ(popKind(jb, &b), Kind::Packet);
    EXPECT_EQ(b, 1);
    ASSERT_EQ(popKind(jb, &b), Kind::Packet);
    EXPECT_EQ(b, 2);
    ASSERT_EQ(popKind(jb, &b), Kind::Packet);
    EXPECT_EQ(b, 3);
}

TEST(JitterBuffer, UsesFecForSingleLossAndPlcForBursts)
{
    JitterBuffer jb(20, 200);
    Feeder f{jb};
    f.push(1, 0ms);
    f.push(3, 40ms); // 2 lost, 3 carries FEC for 2
    f.push(6, 100ms);
    EXPECT_EQ(popKind(jb), Kind::Packet); // 1
    std::uint8_t b = 0;
    EXPECT_EQ(popKind(jb, &b), Kind::Fec); // 2 recovered from 3's FEC
    EXPECT_EQ(b, 3);
    EXPECT_EQ(popKind(jb), Kind::Packet); // 3
    EXPECT_EQ(popKind(jb), Kind::Conceal); // 4 lost, 5 missing too
    EXPECT_EQ(popKind(jb), Kind::Fec); // 5 recovered from 6
    EXPECT_EQ(popKind(jb), Kind::Packet); // 6
    const auto st = jb.stats();
    EXPECT_EQ(st.recoveredFec, 2u);
    EXPECT_EQ(st.concealed, 1u);
}

TEST(JitterBuffer, DropsLatePacketsAndGrowsTarget)
{
    JitterBuffer jb(20, 200);
    Feeder f{jb};
    f.push(1, 0ms);
    f.push(2, 20ms);
    EXPECT_EQ(popKind(jb), Kind::Packet);
    EXPECT_EQ(popKind(jb), Kind::Packet);
    EXPECT_EQ(popKind(jb), Kind::Conceal); // 3 has not arrived
    const int before = jb.stats().targetFrames;
    f.push(3, 90ms); // arrives after its slot
    EXPECT_EQ(jb.stats().late, 1u);
    EXPECT_GT(jb.stats().targetFrames, before) << "late arrivals increase buffering";
}

TEST(JitterBuffer, SkipsAheadWhenLatencyAccumulates)
{
    JitterBuffer jb(20, 200);
    Feeder f{jb};
    for (std::uint32_t s = 1; s <= 12; ++s)
        f.push(s, std::chrono::milliseconds(s)); // a burst after a stall
    std::uint8_t b = 0;
    ASSERT_EQ(popKind(jb, &b), Kind::Packet);
    EXPECT_GT(b, 1) << "old frames are discarded instead of adding delay";
    EXPECT_GT(jb.stats().skipped, 0u);
}

TEST(JitterBuffer, EndOfSpeechRebuffersForNextSpurt)
{
    JitterBuffer jb(40, 200);
    Feeder f{jb};
    f.push(1, 0ms);
    f.push(2, 20ms, true);
    EXPECT_EQ(popKind(jb), Kind::Packet);
    EXPECT_EQ(popKind(jb), Kind::Packet);
    EXPECT_FALSE(jb.stats().playing);
    EXPECT_EQ(popKind(jb), Kind::Silence) << "no concealment noise after a clean end";
    f.push(50, 2000ms);
    f.push(51, 2020ms);
    EXPECT_EQ(popKind(jb), Kind::Packet);
}

TEST(JitterBuffer, StreamStopFallsSilentAfterBriefConcealment)
{
    JitterBuffer jb(20, 200);
    Feeder f{jb};
    f.push(1, 0ms);
    EXPECT_EQ(popKind(jb), Kind::Packet);
    EXPECT_EQ(popKind(jb), Kind::Conceal);
    EXPECT_EQ(popKind(jb), Kind::Conceal);
    EXPECT_EQ(popKind(jb), Kind::Silence);
}
