// Full voice path with a simulated audio device clock:
//   test tone -> A capture -> DSP/VAD -> Opus -> AEAD UDP -> server relay
//   -> B jitter buffer -> Opus -> mixer -> B playback
// Verifies audio arrives with the right pitch, and that mute, deafen,
// push-to-talk, per-user volume and server mute all gate it correctly.

#include "Harness.hpp"

#include <QElapsedTimer>
#include <QJsonArray>

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <vector>

using namespace omachat;
using namespace omachat::test;

namespace {

struct VoiceFixture : ::testing::Test {
    TestServer server;
    std::unique_ptr<TestDaemon> alice;
    std::unique_ptr<TestDaemon> bob;
    double phase = 0;

    void SetUp() override
    {
        ASSERT_TRUE(server.start());
        alice = std::make_unique<TestDaemon>(QStringLiteral("valice"));
        bob = std::make_unique<TestDaemon>(QStringLiteral("vbob"));
        ASSERT_TRUE(alice->start());
        ASSERT_TRUE(bob->start());
        ASSERT_TRUE(alice->registerOn(server, QStringLiteral("alice"), QStringLiteral("alice-password")));
        ASSERT_TRUE(bob->registerOn(server, QStringLiteral("bob"), QStringLiteral("bob-password")));
        ASSERT_TRUE(alice->call(QStringLiteral("server.create"), {{"name", "Voice Lab"}}).ok);
        auto inv = alice->call(QStringLiteral("invite.create"), {{"server", "Voice Lab"}});
        ASSERT_TRUE(bob->call(QStringLiteral("server.join"), {{"invite", inv.result.value("token")}}).ok);
        ASSERT_TRUE(waitFor([&] { return bob->daemon().connection().model().resolveChannel("General") != 0; }));
        ASSERT_TRUE(alice->call(QStringLiteral("audio.set"), {{"mode", "always"}, {"noise_suppression", false}}).ok);
        ASSERT_TRUE(alice->call(QStringLiteral("voice.join"), {{"channel", "General"}}).ok);
        ASSERT_TRUE(bob->call(QStringLiteral("voice.join"), {{"channel", "General"}}).ok);
        ASSERT_TRUE(waitFor(
            [&] {
                return alice->daemon().statusJson().value("voice").toObject().value("registered").toBool()
                    && bob->daemon().statusJson().value("voice").toObject().value("registered").toBool();
            },
            5000))
            << "both media transports must be registered with the relay";
    }

    void TearDown() override
    {
        alice.reset();
        bob.reset();
    }

    // Runs `frames` 20 ms device periods and returns what Bob would hear.
    std::vector<float> run(int frames, double hz = 1000.0, double amplitude = 0.4, qint64* firstAudioMs = nullptr)
    {
        std::vector<float> heard;
        std::vector<float> in(960), out(960);
        QElapsedTimer clock;
        clock.start();
        for (int f = 0; f < frames; ++f) {
            for (auto& x : in) {
                x = static_cast<float>(amplitude * std::sin(phase));
                phase += 2.0 * std::numbers::pi * hz / 48000.0;
            }
            alice->audio().pumpCapture(in.data(), in.size());
            waitFor([] { return false; }, 20);
            bob->audio().pumpPlayback(out.data(), out.size());
            // Also drain Alice's playback so her mixer thread keeps running.
            std::vector<float> sink(960);
            alice->audio().pumpPlayback(sink.data(), sink.size());
            if (firstAudioMs && *firstAudioMs < 0) {
                double e = 0;
                for (float x : out)
                    e += static_cast<double>(x) * x;
                if (e / 960.0 > 1e-4)
                    *firstAudioMs = clock.elapsed();
            }
            heard.insert(heard.end(), out.begin(), out.end());
        }
        return heard;
    }

    static double rms(const std::vector<float>& x, std::size_t from = 0)
    {
        double e = 0;
        for (std::size_t i = from; i < x.size(); ++i)
            e += static_cast<double>(x[i]) * x[i];
        return x.size() > from ? std::sqrt(e / static_cast<double>(x.size() - from)) : 0.0;
    }

    static double dominantHz(const std::vector<float>& x, std::size_t from)
    {
        int crossings = 0;
        for (std::size_t i = from + 1; i < x.size(); ++i)
            crossings += (x[i - 1] < 0 && x[i] >= 0) ? 1 : 0;
        return crossings / (static_cast<double>(x.size() - from) / 48000.0);
    }
};

} // namespace

TEST_F(VoiceFixture, ToneTravelsThroughServerWithCorrectPitch)
{
    qint64 firstAudio = -1;
    const auto heard = run(100, 1000.0, 0.4, &firstAudio);
    const std::size_t tail = heard.size() / 2;
    EXPECT_GT(rms(heard, tail), 0.1) << "Bob must hear Alice";
    EXPECT_NEAR(dominantHz(heard, tail), 1000.0, 60.0);
    ASSERT_GE(firstAudio, 0);
    // Software path only (no device buffers): capture to audible output.
    std::printf("[ voice ] first audio after %lld ms of simulated device time\n", static_cast<long long>(firstAudio));
    EXPECT_LT(firstAudio, 400);

    auto stats = bob->call(QStringLiteral("voice.stats")).result;
    const auto speakers = stats.value("speakers").toArray();
    ASSERT_EQ(speakers.size(), 1);
    std::printf("[ voice ] jitter %.2f ms, target %d ms, lost %d, late %d\n",
        speakers[0].toObject().value("jitter_ms").toDouble(), speakers[0].toObject().value("target_ms").toInt(),
        speakers[0].toObject().value("lost").toInt(), speakers[0].toObject().value("late").toInt());
    EXPECT_TRUE(bob->waitEvent(
        QStringLiteral("voice.speaking"), [](const QJsonObject& e) { return e.value("speaking").toBool(); }, 1000));
}

TEST_F(VoiceFixture, MuteStopsTransmission)
{
    run(40);
    ASSERT_TRUE(alice->call(QStringLiteral("voice.mute")).ok);
    run(30); // let the jitter buffer drain
    const auto heard = run(50);
    EXPECT_LT(rms(heard), 0.01);
    ASSERT_TRUE(alice->call(QStringLiteral("voice.unmute")).ok);
    const auto again = run(60);
    EXPECT_GT(rms(again, again.size() / 2), 0.1);
}

TEST_F(VoiceFixture, DeafenedListenerHearsNothing)
{
    ASSERT_TRUE(bob->call(QStringLiteral("voice.deafen")).ok);
    const auto heard = run(80);
    EXPECT_LT(rms(heard), 0.001);
}

TEST_F(VoiceFixture, PushToTalkGatesTheMicrophone)
{
    ASSERT_TRUE(alice->call(QStringLiteral("voice.mode"), {{"mode", "ptt"}}).ok);
    run(30);
    const auto idle = run(50);
    EXPECT_LT(rms(idle), 0.01) << "no transmission without the key held";
    ASSERT_TRUE(alice->call(QStringLiteral("ptt.begin")).ok);
    const auto talking = run(60);
    EXPECT_GT(rms(talking, talking.size() / 2), 0.1);
    ASSERT_TRUE(alice->call(QStringLiteral("ptt.end")).ok);
    run(30);
    EXPECT_LT(rms(run(40)), 0.01);
}

TEST_F(VoiceFixture, PerUserVolumeIsLocal)
{
    const QString aliceId = alice->daemon().statusJson().value("user").toObject().value("id").toString();
    ASSERT_TRUE(bob->call(QStringLiteral("audio.user_volume"), {{"user", aliceId}, {"volume", 0.0}}).ok);
    EXPECT_LT(rms(run(80)), 0.001);
    ASSERT_TRUE(bob->call(QStringLiteral("audio.user_volume"), {{"user", aliceId}, {"volume", 1.0}}).ok);
    const auto heard = run(60);
    EXPECT_GT(rms(heard, heard.size() / 2), 0.1);
}

TEST_F(VoiceFixture, ServerMuteBlocksAtTheRelay)
{
    run(30);
    const QString bobId = bob->daemon().statusJson().value("user").toObject().value("id").toString();
    const QString aliceId = alice->daemon().statusJson().value("user").toObject().value("id").toString();
    // Bob (a member) cannot server-mute the owner.
    auto denied = bob->call(
        QStringLiteral("moderation.voice_mute"), {{"server", "Voice Lab"}, {"user", aliceId}, {"mute", true}});
    EXPECT_EQ(denied.errorCode, QStringLiteral("PermissionDenied"));
    // The owner mutes herself server-side: the relay drops her packets even
    // though her client keeps sending.
    ASSERT_TRUE(alice
            ->call(
                QStringLiteral("moderation.voice_mute"), {{"server", "Voice Lab"}, {"user", aliceId}, {"mute", true}})
            .ok);
    run(30);
    EXPECT_LT(rms(run(50)), 0.01);
    (void)bobId;
}

TEST_F(VoiceFixture, LeavingStopsForwarding)
{
    run(40);
    ASSERT_TRUE(bob->call(QStringLiteral("voice.leave")).ok);
    EXPECT_FALSE(bob->daemon().voiceEngine().active());
    const auto st = alice->call(QStringLiteral("daemon.status")).result.value("voice").toObject();
    EXPECT_TRUE(waitFor([&] {
        return alice->call(QStringLiteral("daemon.status")).result.value("voice").toObject().value("count").toInt()
            == 1;
    }));
    (void)st;
}
