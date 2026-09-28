#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>

namespace omachat {

// 64-bit sortable identifiers.
//
//   bit 63      : always 0 (keeps ids positive in signed storage such as SQLite)
//   bits 62..22 : 41-bit milliseconds since kSnowflakeEpochMs (~69 years)
//   bits 21..12 : 10-bit node id (0..1023)
//   bits 11..0  : 12-bit per-millisecond sequence (0..4095)
//
// Ids compare in creation order across nodes to millisecond resolution.
inline constexpr std::int64_t kSnowflakeEpochMs = 1735689600000; // 2025-01-01T00:00:00Z
inline constexpr int kSnowflakeTimestampBits = 41;
inline constexpr int kSnowflakeNodeBits = 10;
inline constexpr int kSnowflakeSequenceBits = 12;
inline constexpr std::uint32_t kSnowflakeMaxNode = (1u << kSnowflakeNodeBits) - 1;
inline constexpr std::uint32_t kSnowflakeMaxSequence = (1u << kSnowflakeSequenceBits) - 1;

struct SnowflakeParts {
    std::int64_t unixMs;
    std::uint32_t node;
    std::uint32_t sequence;
};

SnowflakeParts decodeSnowflake(std::uint64_t id);
std::uint64_t encodeSnowflake(std::int64_t unixMs, std::uint32_t node, std::uint32_t sequence);

class SnowflakeGenerator {
public:
    using Clock = std::function<std::int64_t()>; // unix milliseconds

    explicit SnowflakeGenerator(std::uint32_t node, Clock clock = {});

    // Thread-safe. Never returns the same id twice, even if the wall clock
    // moves backwards (the generator keeps using its last timestamp).
    std::uint64_t next();

private:
    std::uint32_t m_node;
    Clock m_clock;
    std::mutex m_mutex;
    std::int64_t m_lastMs = 0;
    std::uint32_t m_sequence = 0;
};

} // namespace omachat
