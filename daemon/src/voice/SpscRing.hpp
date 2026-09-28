#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace omachat::voice {

// Wait-free single-producer/single-consumer ring of samples. Safe to use from
// a realtime audio callback on one side: no locks, no allocation after
// construction.
template <typename T> class SpscRing {
public:
    explicit SpscRing(std::size_t capacity)
        : m_buffer(capacity + 1)
    {
    }

    std::size_t capacity() const { return m_buffer.size() - 1; }

    std::size_t size() const
    {
        const auto w = m_write.load(std::memory_order_acquire);
        const auto r = m_read.load(std::memory_order_acquire);
        return w >= r ? w - r : m_buffer.size() - r + w;
    }

    // Producer side. Writes up to `count` items; returns how many fit.
    std::size_t push(const T* data, std::size_t count)
    {
        const auto w = m_write.load(std::memory_order_relaxed);
        const auto r = m_read.load(std::memory_order_acquire);
        const std::size_t n = m_buffer.size();
        const std::size_t free = r > w ? r - w - 1 : n - w + r - 1;
        const std::size_t todo = count < free ? count : free;
        std::size_t pos = w;
        for (std::size_t i = 0; i < todo; ++i) {
            m_buffer[pos] = data[i];
            pos = pos + 1 == n ? 0 : pos + 1;
        }
        m_write.store(pos, std::memory_order_release);
        return todo;
    }

    // Consumer side. Reads up to `count` items; returns how many were read.
    std::size_t pop(T* out, std::size_t count)
    {
        const auto r = m_read.load(std::memory_order_relaxed);
        const auto w = m_write.load(std::memory_order_acquire);
        const std::size_t n = m_buffer.size();
        const std::size_t available = w >= r ? w - r : n - r + w;
        const std::size_t todo = count < available ? count : available;
        std::size_t pos = r;
        for (std::size_t i = 0; i < todo; ++i) {
            out[i] = m_buffer[pos];
            pos = pos + 1 == n ? 0 : pos + 1;
        }
        m_read.store(pos, std::memory_order_release);
        return todo;
    }

    // Consumer side: drop everything currently buffered.
    void clear() { m_read.store(m_write.load(std::memory_order_acquire), std::memory_order_release); }

private:
    std::vector<T> m_buffer;
    alignas(64) std::atomic<std::size_t> m_write{0};
    alignas(64) std::atomic<std::size_t> m_read{0};
};

} // namespace omachat::voice
