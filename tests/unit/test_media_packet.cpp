#include "omachat/media/MediaPacket.hpp"

#include <gtest/gtest.h>

#include <array>

using namespace omachat::media;

namespace {
Header audioHeader(std::uint32_t seq)
{
    Header h;
    h.type = PacketType::Audio;
    h.streamId = 77;
    h.sequence = seq;
    h.timestamp = seq * 960;
    return h;
}
} // namespace

TEST(MediaPacket, HeaderRoundTripNetworkOrder)
{
    Header h = audioHeader(0x01020304);
    h.senderId = 0x1122334455667788ULL;
    h.flags = FlagEndOfSpeech;
    std::array<std::uint8_t, kHeaderBytes + kTagBytes> buf{};
    writeHeader(h, std::span<std::uint8_t, kHeaderBytes>(buf.data(), kHeaderBytes));
    EXPECT_EQ(buf[16], 0x01); // big-endian sequence
    EXPECT_EQ(buf[19], 0x04);
    EXPECT_EQ(buf[8], 0x11);
    const auto parsed = parseHeader(buf);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->sequence, h.sequence);
    EXPECT_EQ(parsed->senderId, h.senderId);
    EXPECT_EQ(parsed->flags, FlagEndOfSpeech);
}

TEST(MediaPacket, SealOpenRoundTrip)
{
    const Key key = randomKey();
    const std::array<std::uint8_t, 5> payload{1, 2, 3, 4, 5};
    const auto dgram = seal(audioHeader(1), Direction::ClientToServer, key, payload);
    ASSERT_EQ(dgram.size(), kHeaderBytes + payload.size() + kTagBytes);
    const auto h = parseHeader(dgram);
    ASSERT_TRUE(h);
    std::array<std::uint8_t, 64> out{};
    const auto n = open(*h, Direction::ClientToServer, key, dgram, out);
    ASSERT_TRUE(n);
    EXPECT_EQ(*n, payload.size());
    EXPECT_EQ(out[4], 5);
}

TEST(MediaPacket, RejectsTamperingWrongKeyAndWrongDirection)
{
    const Key key = randomKey();
    const std::array<std::uint8_t, 3> payload{9, 9, 9};
    auto dgram = seal(audioHeader(2), Direction::ClientToServer, key, payload);
    std::array<std::uint8_t, 64> out{};

    auto tamperedHeader = dgram;
    tamperedHeader[20] ^= 1; // timestamp is authenticated
    EXPECT_FALSE(open(*parseHeader(tamperedHeader), Direction::ClientToServer, key, tamperedHeader, out));

    auto tamperedBody = dgram;
    tamperedBody.back() ^= 0x80;
    EXPECT_FALSE(open(*parseHeader(tamperedBody), Direction::ClientToServer, key, tamperedBody, out));

    EXPECT_FALSE(open(*parseHeader(dgram), Direction::ClientToServer, randomKey(), dgram, out));
    EXPECT_FALSE(open(*parseHeader(dgram), Direction::ServerToClient, key, dgram, out));
}

TEST(MediaPacket, ParserRejectsGarbage)
{
    std::array<std::uint8_t, 10> tiny{};
    EXPECT_FALSE(parseHeader(tiny));
    std::array<std::uint8_t, 64> bad{};
    bad[0] = 9; // unknown version
    bad[1] = 1;
    EXPECT_FALSE(parseHeader(bad));
    bad[0] = kMediaVersion;
    bad[1] = 99; // unknown type
    EXPECT_FALSE(parseHeader(bad));
    bad[1] = 1;
    bad[3] = 1; // reserved must be zero
    EXPECT_FALSE(parseHeader(bad));
}

TEST(MediaPacket, OversizedPayloadRefused)
{
    std::vector<std::uint8_t> big(kMaxPayloadBytes + 1);
    EXPECT_TRUE(seal(audioHeader(1), Direction::ClientToServer, randomKey(), big).empty());
}

TEST(ReplayWindow, AcceptsNewRejectsDuplicatesAndStale)
{
    ReplayWindow w;
    EXPECT_TRUE(w.accept(100));
    EXPECT_FALSE(w.accept(100));
    EXPECT_TRUE(w.accept(102));
    EXPECT_TRUE(w.accept(101)); // reordered but inside the window
    EXPECT_FALSE(w.accept(101));
    EXPECT_TRUE(w.accept(300));
    EXPECT_FALSE(w.accept(200)); // older than 64 packets
}

TEST(ReplayWindow, HandlesSequenceWraparound)
{
    ReplayWindow w;
    EXPECT_TRUE(w.accept(0xFFFFFFFE));
    EXPECT_TRUE(w.accept(0xFFFFFFFF));
    EXPECT_TRUE(w.accept(0));
    EXPECT_TRUE(w.accept(1));
    EXPECT_FALSE(w.accept(0xFFFFFFFF));
}
