#include "omachat/core/Snowflake.hpp"

#include <algorithm>

namespace omachat {

SnowflakeParts decodeSnowflake(std::uint64_t id)
{
    SnowflakeParts parts{};
    parts.sequence = static_cast<std::uint32_t>(id & kSnowflakeMaxSequence);
    parts.node = static_cast<std::uint32_t>((id >> kSnowflakeSequenceBits) & kSnowflakeMaxNode);
    parts.unixMs = static_cast<std::int64_t>(id >> (kSnowflakeSequenceBits + kSnowflakeNodeBits)) + kSnowflakeEpochMs;
    return parts;
}

std::uint64_t encodeSnowflake(std::int64_t unixMs, std::uint32_t node, std::uint32_t sequence)
{
    const auto ts = static_cast<std::uint64_t>(std::max<std::int64_t>(0, unixMs - kSnowflakeEpochMs))
        & ((std::uint64_t{1} << kSnowflakeTimestampBits) - 1);
    return (ts << (kSnowflakeSequenceBits + kSnowflakeNodeBits))
        | (static_cast<std::uint64_t>(node & kSnowflakeMaxNode) << kSnowflakeSequenceBits)
        | (sequence & kSnowflakeMaxSequence);
}

SnowflakeGenerator::SnowflakeGenerator(std::uint32_t node, Clock clock)
    : m_node(node & kSnowflakeMaxNode)
    , m_clock(std::move(clock))
{
    if (!m_clock) {
        m_clock = [] {
            using namespace std::chrono;
            return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        };
    }
}

std::uint64_t SnowflakeGenerator::next()
{
    std::lock_guard lock(m_mutex);
    std::int64_t now = std::max(m_clock(), m_lastMs);
    if (now == m_lastMs) {
        if (m_sequence >= kSnowflakeMaxSequence) {
            // Sequence exhausted for this millisecond: borrow the next one
            // rather than spinning. Ids stay unique and ordered.
            ++now;
            m_sequence = 0;
        } else {
            ++m_sequence;
        }
    } else {
        m_sequence = 0;
    }
    m_lastMs = now;
    return encodeSnowflake(now, m_node, m_sequence);
}

} // namespace omachat
