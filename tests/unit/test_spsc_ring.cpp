#include "voice/SpscRing.hpp"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

using omachat::voice::SpscRing;

TEST(SpscRing, WrapsAndReportsSize)
{
    SpscRing<int> r(4);
    int in[3] = {1, 2, 3};
    EXPECT_EQ(r.push(in, 3), 3u);
    int out[2];
    EXPECT_EQ(r.pop(out, 2), 2u);
    EXPECT_EQ(out[1], 2);
    int more[3] = {4, 5, 6};
    EXPECT_EQ(r.push(more, 3), 3u); // wraps
    EXPECT_EQ(r.size(), 4u);
    EXPECT_EQ(r.push(more, 1), 0u); // full: never overwrites
    int all[4];
    EXPECT_EQ(r.pop(all, 4), 4u);
    EXPECT_EQ(all[0], 3);
    EXPECT_EQ(all[3], 6);
}

TEST(SpscRing, ConcurrentProducerConsumerPreservesOrder)
{
    SpscRing<std::uint32_t> r(1024);
    constexpr std::uint32_t kCount = 2'000'000;
    std::thread producer([&] {
        std::uint32_t next = 0;
        while (next < kCount) {
            std::uint32_t batch[64];
            std::uint32_t n = 0;
            for (; n < 64 && next + n < kCount; ++n)
                batch[n] = next + n;
            next += static_cast<std::uint32_t>(r.push(batch, n));
        }
    });
    std::uint32_t expected = 0;
    bool ordered = true;
    while (expected < kCount) {
        std::uint32_t buf[128];
        const auto n = r.pop(buf, 128);
        for (std::size_t i = 0; i < n; ++i)
            ordered = ordered && buf[i] == expected++;
    }
    producer.join();
    EXPECT_TRUE(ordered);
}
