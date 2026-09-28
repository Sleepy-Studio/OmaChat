#include "omachat/media/VideoFragments.hpp"

#include <algorithm>
#include <cstring>

namespace omachat::media {

namespace {

void put32(std::uint8_t* p, std::uint32_t v)
{
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

void put16(std::uint8_t* p, std::uint16_t v)
{
    p[0] = static_cast<std::uint8_t>(v >> 8);
    p[1] = static_cast<std::uint8_t>(v);
}

std::uint32_t get32(const std::uint8_t* p)
{
    return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) | std::uint32_t{p[3]};
}

std::uint16_t get16(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

// Serial-number arithmetic: is `a` after `b`?
bool newer(std::uint32_t a, std::uint32_t b)
{
    return a != b && static_cast<std::int32_t>(a - b) > 0;
}

} // namespace

std::vector<std::vector<std::uint8_t>> fragmentFrame(std::uint32_t frameNumber, std::span<const std::uint8_t> frame)
{
    std::vector<std::vector<std::uint8_t>> out;
    if (frame.empty() || frame.size() > kMaxFrameBytes)
        return out;
    const std::size_t count = (frame.size() + kFragmentDataBytes - 1) / kFragmentDataBytes;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t offset = i * kFragmentDataBytes;
        const std::size_t len = std::min(kFragmentDataBytes, frame.size() - offset);
        std::vector<std::uint8_t> p(kFragmentHeaderBytes + len);
        put32(p.data(), frameNumber);
        put16(p.data() + 4, static_cast<std::uint16_t>(i));
        put16(p.data() + 6, static_cast<std::uint16_t>(count));
        std::memcpy(p.data() + kFragmentHeaderBytes, frame.data() + offset, len);
        out.push_back(std::move(p));
    }
    return out;
}

void FrameAssembler::reset()
{
    m_pending.reset();
    m_lastDelivered.reset();
    m_needKeyframe = true;
}

std::optional<FrameAssembler::Frame> FrameAssembler::add(const Header& header, std::span<const std::uint8_t> payload)
{
    if (payload.size() <= kFragmentHeaderBytes)
        return std::nullopt;
    const std::uint32_t number = get32(payload.data());
    const std::uint16_t index = get16(payload.data() + 4);
    const std::uint16_t count = get16(payload.data() + 6);
    const auto bytes = payload.subspan(kFragmentHeaderBytes);
    if (count == 0 || count > kMaxFragments || index >= count || bytes.size() > kFragmentDataBytes)
        return std::nullopt;
    // Only the last fragment may be short.
    if (index + 1 < count && bytes.size() != kFragmentDataBytes)
        return std::nullopt;
    if (m_lastDelivered && !newer(number, *m_lastDelivered))
        return std::nullopt; // late or duplicate

    if (!m_pending || m_pending->number != number) {
        if (m_pending && !newer(number, m_pending->number))
            return std::nullopt; // a fragment of an already abandoned frame
        if (m_pending)
            ++m_lost; // abandoned incomplete
        Pending p;
        p.number = number;
        p.timestamp = header.timestamp;
        p.keyframe = (header.flags & FlagKeyframe) != 0;
        p.count = count;
        p.data.assign(std::size_t{count} * kFragmentDataBytes, 0);
        p.sizes.assign(count, 0);
        m_pending = std::move(p);
    }
    Pending& p = *m_pending;
    if (p.count != count || p.sizes[index] != 0)
        return std::nullopt; // inconsistent or duplicate fragment
    std::memcpy(p.data.data() + std::size_t{index} * kFragmentDataBytes, bytes.data(), bytes.size());
    p.sizes[index] = static_cast<std::uint16_t>(bytes.size());
    if (++p.received < p.count)
        return std::nullopt;

    Frame f;
    f.number = p.number;
    f.timestamp = p.timestamp;
    f.keyframe = p.keyframe;
    f.data = std::move(p.data);
    f.data.resize((std::size_t{p.count} - 1) * kFragmentDataBytes + p.sizes.back());
    m_pending.reset();

    const bool follows = m_lastDelivered && f.number == *m_lastDelivered + 1;
    if (!f.keyframe && (m_needKeyframe || !follows)) {
        // A gap: this frame references something we never decoded.
        if (m_lastDelivered)
            m_lost += f.number - *m_lastDelivered - 1;
        m_needKeyframe = true;
        m_lastDelivered = f.number;
        return std::nullopt;
    }
    m_needKeyframe = false;
    m_lastDelivered = f.number;
    return f;
}

} // namespace omachat::media
