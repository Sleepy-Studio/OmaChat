#include "omachat/media/FrameBuffer.hpp"
#include "omachat/media/VideoFragments.hpp"

#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <random>

using namespace omachat::media;

namespace {

std::vector<std::uint8_t> bytes(std::size_t n, std::uint8_t seed)
{
    std::vector<std::uint8_t> v(n);
    for (std::size_t i = 0; i < n; ++i)
        v[i] = static_cast<std::uint8_t>(seed + i * 7);
    return v;
}

Header videoHeader(bool key, std::uint32_t ts = 0)
{
    Header h;
    h.type = PacketType::Video;
    h.flags = key ? FlagKeyframe : FlagNone;
    h.timestamp = ts;
    return h;
}

// Feeds a whole frame; returns what the assembler delivered, if anything.
std::optional<FrameAssembler::Frame> feed(
    FrameAssembler& a, std::uint32_t number, const std::vector<std::uint8_t>& frame, bool key, bool shuffle = false)
{
    auto parts = fragmentFrame(number, frame);
    if (shuffle)
        std::shuffle(parts.begin(), parts.end(), std::mt19937(number));
    std::optional<FrameAssembler::Frame> out;
    for (const auto& p : parts)
        if (auto f = a.add(videoHeader(key, number * 3000), p))
            out = std::move(f);
    return out;
}

} // namespace

TEST(VideoFragments, SplitsAtThePayloadLimitAndReassemblesInAnyOrder)
{
    const auto frame = bytes(kFragmentDataBytes * 3 + 17, 5);
    const auto parts = fragmentFrame(9, frame);
    ASSERT_EQ(parts.size(), 4u);
    for (const auto& p : parts)
        EXPECT_LE(p.size(), kMaxPayloadBytes);
    FrameAssembler a;
    auto f = feed(a, 9, frame, true, /*shuffle=*/true);
    ASSERT_TRUE(f);
    EXPECT_EQ(f->data, frame);
    EXPECT_TRUE(f->keyframe);
    EXPECT_EQ(f->timestamp, 27000u);
    EXPECT_TRUE(fragmentFrame(1, {}).empty());
}

TEST(VideoFragments, NothingDecodesBeforeAKeyframe)
{
    FrameAssembler a;
    EXPECT_FALSE(feed(a, 1, bytes(100, 1), false));
    EXPECT_TRUE(a.needKeyframe());
    EXPECT_TRUE(feed(a, 2, bytes(100, 2), true));
    EXPECT_FALSE(a.needKeyframe());
    EXPECT_TRUE(feed(a, 3, bytes(100, 3), false));
}

TEST(VideoFragments, ALostFrameStopsDecodingUntilTheNextKeyframe)
{
    FrameAssembler a;
    ASSERT_TRUE(feed(a, 1, bytes(3000, 1), true));
    // Frame 2 loses a fragment; frame 3 arrives whole but references it.
    auto parts = fragmentFrame(2, bytes(3000, 2));
    for (std::size_t i = 1; i < parts.size(); ++i)
        EXPECT_FALSE(a.add(videoHeader(false), parts[i]));
    EXPECT_FALSE(feed(a, 3, bytes(3000, 3), false));
    EXPECT_TRUE(a.needKeyframe());
    EXPECT_GE(a.lostFrames(), 1u);
    EXPECT_FALSE(feed(a, 4, bytes(3000, 4), false)) << "still no keyframe";
    EXPECT_TRUE(feed(a, 5, bytes(3000, 5), true));
    EXPECT_TRUE(feed(a, 6, bytes(3000, 6), false));
}

TEST(VideoFragments, RejectsMalformedAndLateFragments)
{
    FrameAssembler a;
    ASSERT_TRUE(feed(a, 10, bytes(200, 1), true));
    EXPECT_FALSE(feed(a, 10, bytes(200, 1), true)) << "duplicate frame";
    EXPECT_FALSE(feed(a, 8, bytes(200, 1), true)) << "older frame";
    std::vector<std::uint8_t> bad(kFragmentHeaderBytes + 4, 0);
    bad[7] = 0; // count 0
    EXPECT_FALSE(a.add(videoHeader(true), bad));
    bad[5] = 3;
    bad[7] = 2; // index 3 of 2
    EXPECT_FALSE(a.add(videoHeader(true), bad));
    // A short fragment that is not the last one.
    auto parts = fragmentFrame(11, bytes(kFragmentDataBytes * 2, 1));
    parts[0].resize(parts[0].size() - 1);
    EXPECT_FALSE(a.add(videoHeader(true), parts[0]));
}

TEST(FrameBuffer, ReaderSeesCompleteFramesOnly)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("f.frame"));
    FrameBufferWriter w;
    QString error;
    ASSERT_TRUE(w.create(path, &error)) << error.toStdString();
    FrameBufferReader r;
    ASSERT_TRUE(r.open(path));
    FrameBufferReader::Frame f;
    EXPECT_FALSE(r.readIfNewer(0, f)) << "nothing written yet";

    // A 3x2 frame with padded rows.
    std::vector<std::uint8_t> px(16 * 2, 0);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 3; ++x)
            px[static_cast<std::size_t>(y * 16 + x * 4)] = static_cast<std::uint8_t>(10 * y + x);
    ASSERT_TRUE(w.write(px.data(), 3, 2, 16));
    ASSERT_TRUE(r.readIfNewer(0, f));
    EXPECT_EQ(f.width, 3u);
    EXPECT_EQ(f.height, 2u);
    ASSERT_EQ(f.pixels.size(), 24u);
    EXPECT_EQ(f.pixels[12], 10) << "row padding removed";
    EXPECT_FALSE(r.readIfNewer(f.sequence, f)) << "no new frame";
    EXPECT_FALSE(w.write(px.data(), kMaxFrameWidth + 1, 1, (kMaxFrameWidth + 1) * 4));

    w.close();
    EXPECT_FALSE(QFile::exists(path));
    FrameBufferReader junk;
    QFile bogus(dir.filePath(QStringLiteral("bogus")));
    ASSERT_TRUE(bogus.open(QIODevice::WriteOnly));
    bogus.write(QByteArray(128, 'x'));
    bogus.close();
    EXPECT_FALSE(junk.open(bogus.fileName()));
}

#include "video/H264Codec.hpp"

namespace {

// Four solid quadrants in BGRx so colors survive lossy coding recognisably.
std::vector<std::uint8_t> quadrants(int w, int h, int shift)
{
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const bool right = ((x + shift) % w) >= w / 2, bottom = y >= h / 2;
            std::uint8_t* p = &px[(static_cast<std::size_t>(y) * w + x) * 4];
            p[0] = right ? 230 : 20; // B
            p[1] = bottom ? 220 : 30; // G
            p[2] = (right != bottom) ? 200 : 40; // R
            p[3] = 255;
        }
    }
    return px;
}

} // namespace

TEST(H264, EncodesAndDecodesWithEveryAvailableEncoder)
{
    using namespace omachat::video;
    const QStringList encoders = H264Encoder::available();
    ASSERT_FALSE(encoders.isEmpty()) << "FFmpeg has no H.264 encoder";
    for (const QString& name : encoders) {
        SCOPED_TRACE(name.toStdString());
        H264Encoder enc;
        H264Encoder::Settings s;
        s.encoder = name;
        s.maxWidth = 640;
        s.maxHeight = 360;
        QString error;
        if (!enc.open(1280, 720, PixelFormat::BGRx, s, &error)) {
            // Hardware encoders are listed when FFmpeg supports them even
            // if this machine has no such GPU; only libx264 must work.
            EXPECT_NE(name, QStringLiteral("x264")) << error.toStdString();
            continue;
        }
        EXPECT_EQ(enc.width(), 640) << "scaled into the limit";
        EXPECT_EQ(enc.height(), 360);

        H264Decoder dec;
        ASSERT_TRUE(dec.open(&error)) << error.toStdString();
        int decoded = 0, lastW = 0, lastH = 0;
        std::vector<std::uint8_t> last;
        bool sawKey = false;
        for (int i = 0; i < 12; ++i) {
            const auto px = quadrants(1280, 720, 0);
            std::vector<std::uint8_t> au;
            bool key = false;
            ASSERT_TRUE(enc.encode(px.data(), 1280 * 4, i * 3000, i == 0 || i == 6, au, key));
            if (au.empty())
                continue;
            sawKey = sawKey || key;
            dec.decode(au, [&](const std::uint8_t* bgra, int w, int h, int stride) {
                ++decoded;
                lastW = w;
                lastH = h;
                last.assign(bgra, bgra + static_cast<std::size_t>(stride) * h);
            });
        }
        EXPECT_TRUE(sawKey);
        EXPECT_GE(decoded, 6);
        ASSERT_EQ(lastW, 640);
        ASSERT_EQ(lastH, 360);
        // Top-left quadrant is (B 20, G 30, R 40); bottom-right (230, 220, 40).
        auto at = [&](int x, int y) { return &last[(static_cast<std::size_t>(y) * lastW + x) * 4]; };
        EXPECT_NEAR(at(80, 80)[0], 20, 16);
        EXPECT_NEAR(at(80, 80)[1], 30, 16);
        EXPECT_NEAR(at(560, 300)[0], 230, 16);
        EXPECT_NEAR(at(560, 300)[1], 220, 16);
        EXPECT_NEAR(at(560, 80)[2], 200, 16);
    }
}
