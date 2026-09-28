#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <span>

namespace omachat::voice {

// Adaptive per-speaker jitter buffer for 20 ms Opus frames.
//
// Optimized for conversation: it starts playout as soon as `target` frames
// are queued, never waits for retransmission, conceals gaps with Opus FEC or
// PLC, drops late packets, and skips ahead when the queue grows beyond the
// target so latency cannot accumulate.
//
// push() and pop() may be called from different (non-realtime) threads.
class JitterBuffer {
public:
    static constexpr int kSlots = 64;
    static constexpr int kMaxPayload = 1275;
    static constexpr int kFrameMs = 20;

    JitterBuffer(int minMs = 20, int maxMs = 200);

    using Clock = std::chrono::steady_clock;

    void push(std::uint32_t sequence, std::uint32_t timestamp, std::span<const std::uint8_t> payload, bool endOfSpeech,
        Clock::time_point arrival = Clock::now());

    enum class Kind { Packet, Fec, Conceal, Silence };
    struct Slot {
        Kind kind = Kind::Silence;
        std::array<std::uint8_t, kMaxPayload> data{};
        std::size_t size = 0;
    };

    // Produces the decode instruction for the next 20 ms of playout.
    void pop(Slot& out);

    struct Stats {
        std::uint64_t received = 0;
        std::uint64_t late = 0;
        std::uint64_t lost = 0;
        std::uint64_t recoveredFec = 0;
        std::uint64_t concealed = 0;
        std::uint64_t skipped = 0;
        int targetFrames = 0;
        int bufferedFrames = 0;
        double jitterMs = 0;
        bool playing = false;
    };
    Stats stats() const;

    void setLimits(int minMs, int maxMs);
    void reset();

private:
    struct Entry {
        bool valid = false;
        bool endOfSpeech = false;
        std::uint32_t sequence = 0;
        std::size_t size = 0;
        std::array<std::uint8_t, kMaxPayload> data{};
    };

    Entry& slot(std::uint32_t seq) { return m_entries[seq % kSlots]; }
    bool has(std::uint32_t seq) const
    {
        const Entry& e = m_entries[seq % kSlots];
        return e.valid && e.sequence == seq;
    }
    int bufferedLocked() const;
    int computeTarget() const;
    void dropThrough(std::uint32_t seq);

    mutable std::mutex m_mutex;
    std::array<Entry, kSlots> m_entries{};
    bool m_playing = false;
    std::uint32_t m_next = 0;
    std::uint32_t m_highest = 0;
    bool m_haveHighest = false;
    int m_underruns = 0;
    bool m_endSeen = false;

    int m_minFrames = 1;
    int m_maxFrames = 10;
    int m_lateBias = 0; // extra frames added after late arrivals

    // RFC 3550 interarrival jitter estimate (milliseconds).
    double m_jitterMs = 0;
    bool m_haveTransit = false;
    double m_lastTransitMs = 0;
    Clock::time_point m_epoch = Clock::now();

    Stats m_stats;
};

} // namespace omachat::voice
