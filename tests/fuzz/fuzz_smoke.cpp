// Deterministic random + mutation testing of every untrusted-input parser.
// Runs in CI on every build; the libfuzzer_* harnesses go deeper on demand.

#include "FuzzTargets.hpp"

#include "crypto/E2E.hpp"
#include "network.pb.h"
#include "omachat/media/MediaPacket.hpp"
#include "omachat/media/VideoFragments.hpp"
#include "omachat/protocol/Framing.hpp"

#include <gtest/gtest.h>

#include <random>
#include <vector>

using namespace omachat;

namespace {

std::vector<std::uint8_t> randomBytes(std::mt19937& rng, std::size_t maxLen)
{
    std::uniform_int_distribution<std::size_t> len(0, maxLen);
    std::uniform_int_distribution<int> byte(0, 255);
    std::vector<std::uint8_t> v(len(rng));
    for (auto& b : v)
        b = static_cast<std::uint8_t>(byte(rng));
    return v;
}

void mutate(std::mt19937& rng, std::vector<std::uint8_t>& v)
{
    std::uniform_int_distribution<int> kind(0, 3);
    std::uniform_int_distribution<int> byte(0, 255);
    for (int i = 0; i < 4 && !v.empty(); ++i) {
        // Positions are drawn against the current size: edits change it.
        std::uniform_int_distribution<std::size_t> pos(0, v.size() - 1);
        switch (kind(rng)) {
        case 0:
            v[pos(rng)] = static_cast<std::uint8_t>(byte(rng));
            break;
        case 1:
            v[pos(rng)] ^= static_cast<std::uint8_t>(1 << (byte(rng) % 8));
            break;
        case 2:
            v.resize(pos(rng) + 1);
            break;
        default:
            v.insert(v.begin() + static_cast<std::ptrdiff_t>(pos(rng)), static_cast<std::uint8_t>(byte(rng)));
            break;
        }
    }
}

std::vector<std::uint8_t> sampleEnvelope()
{
    proto::Envelope env;
    auto* e = env.mutable_event();
    e->set_sequence(5);
    auto* m = e->mutable_message_create();
    m->set_id(1);
    m->set_channel_id(2);
    m->set_content("hello");
    m->add_mention_ids(3);
    const std::string s = env.SerializeAsString();
    return {s.begin(), s.end()};
}

} // namespace

TEST(FuzzSmoke, FramingSurvivesRandomAndMutatedInput)
{
    std::mt19937 rng(0xF00D);
    const auto env = sampleEnvelope();
    const QByteArray frame = protocol::encodeFrame(
        QByteArray(reinterpret_cast<const char*>(env.data()), static_cast<qsizetype>(env.size())));
    for (int i = 0; i < 20000; ++i) {
        auto bytes = randomBytes(rng, 512);
        fuzz::framing(bytes.data(), bytes.size());
        std::vector<std::uint8_t> valid(frame.begin(), frame.end());
        mutate(rng, valid);
        fuzz::framing(valid.data(), valid.size());
    }
    SUCCEED();
}

TEST(FuzzSmoke, MediaHeadersSurviveRandomAndMutatedInput)
{
    std::mt19937 rng(0xBEEF);
    media::Header h;
    h.type = media::PacketType::Audio;
    h.streamId = 1;
    h.sequence = 10;
    const std::array<std::uint8_t, 40> payload{};
    const auto valid = media::seal(h, media::Direction::ServerToClient, media::randomKey(), payload);
    for (int i = 0; i < 20000; ++i) {
        auto bytes = randomBytes(rng, 1500);
        if (bytes.size() > 4) {
            bytes[0] = media::kMediaVersion; // get past the version check often
            bytes[3] = 0;
            bytes[1] = static_cast<std::uint8_t>(1 + i % 3);
        }
        fuzz::mediaHeader(bytes.data(), bytes.size());
        auto m = valid;
        mutate(rng, m);
        fuzz::mediaHeader(m.data(), m.size());
    }
    SUCCEED();
}

TEST(FuzzSmoke, ProtobufBoundariesSurviveMutation)
{
    std::mt19937 rng(0xCAFE);
    const auto base = sampleEnvelope();
    for (int i = 0; i < 20000; ++i) {
        auto m = base;
        mutate(rng, m);
        fuzz::envelope(m.data(), m.size());
        auto r = randomBytes(rng, 256);
        fuzz::envelope(r.data(), r.size());
    }
    SUCCEED();
}

TEST(FuzzSmoke, IpcLinesSurviveGarbage)
{
    std::mt19937 rng(0x1234);
    const std::string sample = R"({"v":1,"id":"9","method":"voice.join","params":{"channel":"123"}})"
                               "\n";
    for (int i = 0; i < 20000; ++i) {
        std::vector<std::uint8_t> m(sample.begin(), sample.end());
        mutate(rng, m);
        m.push_back('\n');
        fuzz::ipcLine(m.data(), m.size());
        auto r = randomBytes(rng, 256);
        fuzz::ipcLine(r.data(), r.size());
    }
    SUCCEED();
}

TEST(FuzzSmoke, VideoFragmentsSurviveRandomAndMutatedInput)
{
    std::mt19937 rng(0xF1A3);
    // Valid fragments of a few frames, then mutated copies.
    std::vector<std::uint8_t> valid;
    for (std::uint32_t n = 1; n <= 3; ++n) {
        std::vector<std::uint8_t> frame(3000 + n * 500, static_cast<std::uint8_t>(n));
        for (const auto& part : media::fragmentFrame(n, frame)) {
            valid.push_back(n == 1 ? media::FlagKeyframe : 0);
            valid.push_back(static_cast<std::uint8_t>(part.size() >> 8));
            valid.push_back(static_cast<std::uint8_t>(part.size()));
            valid.insert(valid.end(), part.begin(), part.end());
        }
    }
    fuzz::videoFragments(valid.data(), valid.size());
    for (int i = 0; i < 5000; ++i) {
        auto bytes = randomBytes(rng, 4000);
        fuzz::videoFragments(bytes.data(), bytes.size());
        auto m = valid;
        mutate(rng, m);
        fuzz::videoFragments(m.data(), m.size());
    }
    SUCCEED();
}

TEST(FuzzSmoke, EncryptedPayloadsSurviveRandomAndMutatedInput)
{
    std::mt19937 rng(0xE2E0);
    const auto me = e2e::Identity::generate();
    proto::E2EBody body;
    body.set_content("hello");
    const auto sealed = e2e::seal(body, {1, 2}, e2e::Identity::generate(), {me.publicBytes()});
    ASSERT_TRUE(sealed);
    for (int i = 0; i < 5000; ++i) {
        auto bytes = randomBytes(rng, 600);
        fuzz::e2ePayload(bytes.data(), bytes.size());
        std::vector<std::uint8_t> m(sealed->begin(), sealed->end());
        mutate(rng, m);
        fuzz::e2ePayload(m.data(), m.size());
    }
    SUCCEED();
}
