#include "omachat/core/Snowflake.hpp"

#include <gtest/gtest.h>

#include <set>
#include <thread>
#include <vector>

using namespace omachat;

TEST(Snowflake, EncodeDecodeRoundTrip)
{
    const std::int64_t t = kSnowflakeEpochMs + 123456789;
    const auto id = encodeSnowflake(t, 42, 7);
    const auto parts = decodeSnowflake(id);
    EXPECT_EQ(parts.unixMs, t);
    EXPECT_EQ(parts.node, 42u);
    EXPECT_EQ(parts.sequence, 7u);
    EXPECT_EQ(id >> 63, 0u) << "top bit must stay clear for signed storage";
}

TEST(Snowflake, IdsAreStrictlyIncreasingWithinAMillisecond)
{
    std::int64_t now = kSnowflakeEpochMs + 1000;
    SnowflakeGenerator gen(3, [&] { return now; });
    std::uint64_t prev = 0;
    for (int i = 0; i < 5000; ++i) { // exceeds the 4096 per-ms sequence space
        const auto id = gen.next();
        EXPECT_GT(id, prev);
        prev = id;
    }
}

TEST(Snowflake, ClockGoingBackwardsNeverRepeats)
{
    std::int64_t now = kSnowflakeEpochMs + 50000;
    SnowflakeGenerator gen(1, [&] { return now; });
    const auto a = gen.next();
    now -= 10000; // NTP step backwards
    const auto b = gen.next();
    EXPECT_GT(b, a);
}

TEST(Snowflake, ThreadSafeUniqueness)
{
    SnowflakeGenerator gen(9);
    std::vector<std::vector<std::uint64_t>> results(4);
    std::vector<std::thread> threads;
    for (auto& r : results)
        threads.emplace_back([&gen, &r] {
            for (int i = 0; i < 20000; ++i)
                r.push_back(gen.next());
        });
    for (auto& t : threads)
        t.join();
    std::set<std::uint64_t> all;
    for (const auto& r : results)
        all.insert(r.begin(), r.end());
    EXPECT_EQ(all.size(), 80000u);
}

TEST(Snowflake, NodeIsMasked)
{
    const auto parts = decodeSnowflake(encodeSnowflake(kSnowflakeEpochMs, 5000, 0));
    EXPECT_LE(parts.node, kSnowflakeMaxNode);
}
