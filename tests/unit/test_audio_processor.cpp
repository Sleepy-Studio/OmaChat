#include "voice/AudioProcessor.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <numbers>
#include <random>

using omachat::voice::AudioProcessor;

namespace {
std::array<float, 960> tone(double hz, double amp, int& phase)
{
    std::array<float, 960> f{};
    for (auto& x : f)
        x = static_cast<float>(amp * std::sin(2.0 * std::numbers::pi * hz * (phase++) / 48000.0));
    return f;
}
} // namespace

TEST(AudioProcessor, VadDetectsLoudFramesAndHangsOver)
{
    AudioProcessor p;
    AudioProcessor::Settings s;
    s.noiseSuppression = false;
    s.highPass = false; // isolate hangover from the filter's decay tail
    s.vadThresholdDb = -40;
    s.vadHangoverMs = 100; // 5 frames
    p.configure(s);
    int phase = 0;
    auto quiet = tone(300, 0.0005, phase);
    EXPECT_FALSE(p.process(quiet).voice);
    auto loud = tone(300, 0.3, phase);
    const auto r = p.process(loud);
    EXPECT_TRUE(r.voice);
    EXPECT_GT(r.levelDb, -20.0f);
    int voicedAfter = 0;
    for (int i = 0; i < 10; ++i) {
        auto q = tone(300, 0.0005, phase);
        voicedAfter += p.process(q).voice ? 1 : 0;
    }
    EXPECT_EQ(voicedAfter, 4) << "hangover keeps transmitting briefly after speech";
}

TEST(AudioProcessor, HighPassRemovesDcAndRumble)
{
    AudioProcessor p;
    AudioProcessor::Settings s;
    s.noiseSuppression = false;
    s.highPass = true;
    p.configure(s);
    std::array<float, 960> dc{};
    float last = 1.0f;
    for (int i = 0; i < 50; ++i) {
        dc.fill(0.5f);
        p.process(dc);
        last = dc.back();
    }
    EXPECT_LT(std::abs(last), 0.01f);
}

TEST(AudioProcessor, InputGainApplied)
{
    AudioProcessor p;
    AudioProcessor::Settings s;
    s.noiseSuppression = false;
    s.highPass = false;
    s.inputGain = 0.5;
    p.configure(s);
    std::array<float, 960> f{};
    f.fill(0.4f);
    p.process(f);
    EXPECT_FLOAT_EQ(f[0], 0.2f);
}

TEST(AudioProcessor, NoiseSuppressionRunsWhenAvailable)
{
    if (!AudioProcessor::noiseSuppressionAvailable())
        GTEST_SKIP() << "built without RNNoise";
    AudioProcessor p;
    AudioProcessor::Settings s;
    s.noiseSuppression = true;
    s.highPass = false;
    p.configure(s);
    std::mt19937 rng(1);
    std::normal_distribution<float> noise(0.0f, 0.05f);
    double inE = 0, outE = 0;
    for (int i = 0; i < 100; ++i) {
        std::array<float, 960> f{};
        for (auto& x : f)
            x = noise(rng);
        for (float x : f)
            inE += static_cast<double>(x) * x;
        p.process(f);
        if (i > 20)
            for (float x : f)
                outE += static_cast<double>(x) * x;
    }
    EXPECT_LT(outE, inE * 0.5) << "stationary noise should be attenuated";
}
