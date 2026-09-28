#include "voice/JitterBuffer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace omachat::voice {

namespace {
// Serial-number comparison: true if a is after b (RFC 1982 style).
bool after(std::uint32_t a, std::uint32_t b)
{
    return static_cast<std::int32_t>(a - b) > 0;
}
} // namespace

JitterBuffer::JitterBuffer(int minMs, int maxMs)
{
    setLimits(minMs, maxMs);
}

void JitterBuffer::setLimits(int minMs, int maxMs)
{
    std::lock_guard lock(m_mutex);
    m_minFrames = std::max(1, minMs / kFrameMs);
    m_maxFrames = std::max(m_minFrames, maxMs / kFrameMs);
}

void JitterBuffer::reset()
{
    std::lock_guard lock(m_mutex);
    for (auto& e : m_entries)
        e.valid = false;
    m_playing = false;
    m_haveHighest = false;
    m_underruns = 0;
    m_endSeen = false;
    m_haveTransit = false;
    m_jitterMs = 0;
    m_lateBias = 0;
}

int JitterBuffer::bufferedLocked() const
{
    int n = 0;
    for (const auto& e : m_entries) {
        if (e.valid && (!m_playing || !after(m_next, e.sequence)))
            ++n;
    }
    return n;
}

int JitterBuffer::computeTarget() const
{
    // Two jitter deviations cover ~95% of arrivals; add a small margin.
    const int fromJitter = static_cast<int>(std::ceil((2.0 * m_jitterMs + 5.0) / kFrameMs));
    return std::clamp(fromJitter + m_lateBias, m_minFrames, m_maxFrames);
}

void JitterBuffer::push(std::uint32_t sequence, std::uint32_t timestamp, std::span<const std::uint8_t> payload,
    bool endOfSpeech, Clock::time_point arrival)
{
    if (payload.empty() || payload.size() > static_cast<std::size_t>(kMaxPayload))
        return;
    std::lock_guard lock(m_mutex);
    ++m_stats.received;

    // Interarrival jitter: variation of (arrival - send timestamp).
    const double arrivalMs = std::chrono::duration<double, std::milli>(arrival - m_epoch).count();
    const double transit = arrivalMs - static_cast<double>(timestamp) / 48.0;
    if (m_haveTransit) {
        const double d = std::abs(transit - m_lastTransitMs);
        // Ignore talk-spurt boundaries (timestamps jump when silent).
        if (d < 1000.0)
            m_jitterMs += (d - m_jitterMs) / 16.0;
    }
    m_haveTransit = true;
    m_lastTransitMs = transit;

    if (m_playing && !after(sequence, m_next - 1)) {
        // Arrived after its playout slot: useless now. Grow the buffer a bit.
        ++m_stats.late;
        m_lateBias = std::min(m_lateBias + 1, 4);
        return;
    }
    Entry& e = slot(sequence);
    e.valid = true;
    e.sequence = sequence;
    e.endOfSpeech = endOfSpeech;
    e.size = payload.size();
    std::memcpy(e.data.data(), payload.data(), payload.size());
    if (!m_haveHighest || after(sequence, m_highest)) {
        m_highest = sequence;
        m_haveHighest = true;
    }
    if (endOfSpeech)
        m_endSeen = true;
}

void JitterBuffer::dropThrough(std::uint32_t seq)
{
    for (auto& e : m_entries) {
        if (e.valid && !after(e.sequence, seq))
            e.valid = false;
    }
}

void JitterBuffer::pop(Slot& out)
{
    std::lock_guard lock(m_mutex);
    out.kind = Kind::Silence;
    out.size = 0;

    if (!m_playing) {
        if (!m_haveHighest)
            return;
        const int buffered = bufferedLocked();
        const int target = computeTarget();
        // Start once the target depth is reached, or immediately for a
        // complete short burst that already ended.
        if (buffered < target && !(m_endSeen && buffered > 0))
            return;
        // Begin at the oldest buffered frame.
        std::uint32_t oldest = m_highest;
        for (const auto& e : m_entries) {
            if (e.valid && after(oldest, e.sequence))
                oldest = e.sequence;
        }
        m_next = oldest;
        m_playing = true;
        m_underruns = 0;
        m_endSeen = false;
    }

    // Latency cap: if we have fallen behind, skip ahead to the target depth.
    const int target = computeTarget();
    const int buffered = bufferedLocked();
    if (buffered > target + 3) {
        const int skip = buffered - target;
        dropThrough(m_next + static_cast<std::uint32_t>(skip) - 1);
        m_next += static_cast<std::uint32_t>(skip);
        m_stats.skipped += static_cast<std::uint64_t>(skip);
    }

    const std::uint32_t seq = m_next++;
    if (has(seq)) {
        Entry& e = slot(seq);
        out.kind = Kind::Packet;
        out.size = e.size;
        std::memcpy(out.data.data(), e.data.data(), e.size);
        e.valid = false;
        m_underruns = 0;
        if (e.endOfSpeech) {
            // Talk spurt finished cleanly: rebuffer for the next one, keeping
            // any frames of a new spurt that already arrived.
            m_playing = false;
            dropThrough(seq);
            m_haveHighest = false;
            for (const auto& other : m_entries) {
                if (other.valid && (!m_haveHighest || after(other.sequence, m_highest))) {
                    m_highest = other.sequence;
                    m_haveHighest = true;
                }
            }
            m_lateBias = std::max(0, m_lateBias - 1);
        }
        return;
    }

    ++m_stats.lost;
    if (has(seq + 1)) {
        const Entry& next = slot(seq + 1);
        out.kind = Kind::Fec;
        out.size = next.size;
        std::memcpy(out.data.data(), next.data.data(), next.size);
        ++m_stats.recoveredFec;
        m_underruns = 0;
        return;
    }

    const bool anyNewer = m_haveHighest && after(m_highest, seq);
    if (anyNewer) {
        out.kind = Kind::Conceal;
        ++m_stats.concealed;
        return;
    }

    // Nothing buffered: the speaker paused or packets stopped. Conceal a
    // couple of frames, then fall silent and rebuffer.
    if (++m_underruns <= 2) {
        out.kind = Kind::Conceal;
        ++m_stats.concealed;
        return;
    }
    m_playing = false;
    m_haveHighest = false;
    out.kind = Kind::Silence;
}

JitterBuffer::Stats JitterBuffer::stats() const
{
    std::lock_guard lock(m_mutex);
    Stats s = m_stats;
    s.targetFrames = computeTarget();
    s.bufferedFrames = bufferedLocked();
    s.jitterMs = m_jitterMs;
    s.playing = m_playing;
    return s;
}

} // namespace omachat::voice
