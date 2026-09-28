#pragma once

#include <algorithm>
#include <chrono>

namespace omachat {

// Token bucket. `capacity` tokens, refilled continuously at `ratePerSecond`.
// Not thread-safe; each owner keeps its own bucket.
class TokenBucket {
public:
    using Clock = std::chrono::steady_clock;

    TokenBucket(double capacity, double ratePerSecond)
        : m_capacity(capacity)
        , m_rate(ratePerSecond)
        , m_tokens(capacity)
        , m_last(Clock::now())
    {
    }

    bool tryConsume(double tokens = 1.0) { return tryConsumeAt(Clock::now(), tokens); }

    bool tryConsumeAt(Clock::time_point now, double tokens = 1.0)
    {
        refill(now);
        if (m_tokens < tokens)
            return false;
        m_tokens -= tokens;
        return true;
    }

    // Milliseconds until `tokens` would be available.
    int retryAfterMs(double tokens = 1.0) const
    {
        if (m_tokens >= tokens || m_rate <= 0)
            return 0;
        return static_cast<int>(((tokens - m_tokens) / m_rate) * 1000.0) + 1;
    }

private:
    void refill(Clock::time_point now)
    {
        const std::chrono::duration<double> elapsed = now - m_last;
        m_last = now;
        m_tokens = std::min(m_capacity, m_tokens + elapsed.count() * m_rate);
    }

    double m_capacity;
    double m_rate;
    double m_tokens;
    Clock::time_point m_last;
};

// Exponential backoff with full jitter (AWS architecture blog variant).
class Backoff {
public:
    Backoff(int baseMs, int capMs)
        : m_base(baseMs)
        , m_cap(capMs)
    {
    }

    // `random01` must return a uniformly distributed value in [0, 1).
    template <typename Random> int nextDelayMs(Random&& random01)
    {
        const int exp = std::min(m_attempt, 16);
        const double ceiling = std::min<double>(m_cap, static_cast<double>(m_base) * (1 << exp));
        ++m_attempt;
        // Never return 0 so reconnect loops cannot spin.
        return std::max(m_base / 2, static_cast<int>(random01() * ceiling));
    }

    void reset() { m_attempt = 0; }
    int attempts() const { return m_attempt; }

private:
    int m_base;
    int m_cap;
    int m_attempt = 0;
};

} // namespace omachat
