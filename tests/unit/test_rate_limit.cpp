#include "omachat/core/RateLimiter.hpp"

#include <gtest/gtest.h>

using namespace omachat;
using namespace std::chrono_literals;

TEST(TokenBucket, BurstThenRefill)
{
    TokenBucket b(3, 1.0);
    const auto t0 = TokenBucket::Clock::now();
    EXPECT_TRUE(b.tryConsumeAt(t0));
    EXPECT_TRUE(b.tryConsumeAt(t0));
    EXPECT_TRUE(b.tryConsumeAt(t0));
    EXPECT_FALSE(b.tryConsumeAt(t0));
    EXPECT_GT(b.retryAfterMs(), 0);
    EXPECT_FALSE(b.tryConsumeAt(t0 + 500ms));
    EXPECT_TRUE(b.tryConsumeAt(t0 + 1600ms));
}

TEST(TokenBucket, NeverExceedsCapacity)
{
    TokenBucket b(2, 100.0);
    const auto t0 = TokenBucket::Clock::now();
    const auto later = t0 + 10s;
    EXPECT_TRUE(b.tryConsumeAt(later));
    EXPECT_TRUE(b.tryConsumeAt(later));
    EXPECT_FALSE(b.tryConsumeAt(later));
}

TEST(TokenBucket, NormalVoiceRateIsNotPenalized)
{
    // The relay's audio bucket must pass 50 packets/s indefinitely.
    TokenBucket b(400, 200);
    auto t = TokenBucket::Clock::now();
    for (int i = 0; i < 50 * 60; ++i) {
        ASSERT_TRUE(b.tryConsumeAt(t));
        t += 20ms;
    }
}

TEST(Backoff, GrowsWithJitterAndCaps)
{
    Backoff b(500, 30000);
    int last = 0;
    for (int i = 0; i < 20; ++i) {
        const int d = b.nextDelayMs([] { return 0.999; });
        EXPECT_GE(d, 250);
        EXPECT_LE(d, 30000);
        EXPECT_GE(d, last);
        last = d;
    }
    EXPECT_EQ(last, 29970);
    b.reset();
    EXPECT_LE(b.nextDelayMs([] { return 0.999; }), 500);
    // Full jitter may choose small delays but never zero.
    EXPECT_GT(b.nextDelayMs([] { return 0.0; }), 0);
}
