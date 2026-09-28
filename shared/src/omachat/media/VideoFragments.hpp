#pragma once

#include "omachat/media/MediaPacket.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace omachat::media {

// Screen-share frames (H.264 access units, Annex B) are larger than one
// datagram, so each is split into fragments. Every video packet payload is
//
//   off size field
//   0   4    frame number     per stream, increments by one per frame
//   4   2    fragment index   0 .. count-1
//   6   2    fragment count   1 .. kMaxFragments
//   8   n    bytes of the access unit
//
// The media header's keyframe flag is set on every fragment of an IDR frame
// and its timestamp is the 90 kHz capture clock.
inline constexpr std::size_t kFragmentHeaderBytes = 8;
inline constexpr std::size_t kFragmentDataBytes = kMaxPayloadBytes - kFragmentHeaderBytes;
inline constexpr std::size_t kMaxFragments = 4096; // ~5.5 MB: far above any sane frame
inline constexpr std::size_t kMaxFrameBytes = kMaxFragments * kFragmentDataBytes;

// Splits one frame into payloads. Returns empty if the frame is empty or too large.
std::vector<std::vector<std::uint8_t>> fragmentFrame(std::uint32_t frameNumber, std::span<const std::uint8_t> frame);

// Rebuilds frames of one sender. Frames complete in any fragment order;
// a newer frame abandons unfinished older ones. `needKeyframe()` reports
// that frames were lost since the last keyframe, so decoding would show
// garbage until the next one: the receiver should ask for it.
class FrameAssembler {
public:
    struct Frame {
        std::uint32_t number = 0;
        std::uint32_t timestamp = 0;
        bool keyframe = false;
        std::vector<std::uint8_t> data;
    };

    // Returns a complete frame when this fragment finishes one that may be
    // decoded (a keyframe, or the successor of the last delivered frame).
    std::optional<Frame> add(const Header& header, std::span<const std::uint8_t> payload);

    bool needKeyframe() const { return m_needKeyframe; }
    std::uint64_t lostFrames() const { return m_lost; }
    void reset();

private:
    struct Pending {
        std::uint32_t number = 0;
        std::uint32_t timestamp = 0;
        bool keyframe = false;
        std::uint16_t count = 0;
        std::uint16_t received = 0;
        std::vector<std::uint8_t> data;
        std::vector<std::uint16_t> sizes; // 0 = missing
    };

    std::optional<Pending> m_pending;
    std::optional<std::uint32_t> m_lastDelivered;
    bool m_needKeyframe = true; // nothing decodable before the first keyframe
    std::uint64_t m_lost = 0;
};

} // namespace omachat::media
