#include "network.pb.h"
#include "omachat/ipc/IpcMessage.hpp"
#include "omachat/protocol/Framing.hpp"

#include <gtest/gtest.h>

using namespace omachat;

TEST(Framing, RoundTripAcrossArbitraryChunks)
{
    protocol::FrameDecoder dec;
    QByteArray stream;
    for (int i = 0; i < 50; ++i)
        stream += protocol::encodeFrame(QByteArray(i * 7, static_cast<char>('a' + i % 26)));
    // Feed one byte at a time: the decoder must reassemble exactly.
    int frames = 0;
    for (char c : stream) {
        dec.feed(QByteArray(1, c));
        QByteArray f;
        while (dec.next(f) == protocol::FrameDecoder::Status::Frame) {
            EXPECT_EQ(f.size(), frames * 7);
            ++frames;
        }
    }
    EXPECT_EQ(frames, 50);
    EXPECT_EQ(dec.buffered(), 0);
}

TEST(Framing, OversizedFrameIsRejectedBeforeBuffering)
{
    protocol::FrameDecoder dec(1024);
    QByteArray header(4, 0);
    header[0] = 0x7f; // ~2 GB claimed length
    dec.feed(header);
    QByteArray f;
    EXPECT_EQ(dec.next(f), protocol::FrameDecoder::Status::Oversized);
}

TEST(Framing, EnvelopeSurvivesFraming)
{
    proto::Envelope env;
    env.set_request_id(99);
    env.mutable_send_message()->set_channel_id(5);
    env.mutable_send_message()->set_content("héllo **world**");
    const QByteArray frame = protocol::encodeFrame(QByteArray::fromStdString(env.SerializeAsString()));
    protocol::FrameDecoder dec;
    dec.feed(frame);
    QByteArray payload;
    ASSERT_EQ(dec.next(payload), protocol::FrameDecoder::Status::Frame);
    proto::Envelope out;
    ASSERT_TRUE(out.ParseFromArray(payload.constData(), static_cast<int>(payload.size())));
    EXPECT_EQ(out.request_id(), 99u);
    EXPECT_EQ(out.send_message().content(), "héllo **world**");
}

TEST(IpcLines, SplitsAndLimits)
{
    ipc::LineDecoder dec;
    dec.feed("{\"a\":1}\n{\"b\"");
    QByteArray line;
    ASSERT_EQ(dec.next(line), ipc::LineDecoder::Status::Line);
    EXPECT_EQ(line, "{\"a\":1}");
    EXPECT_EQ(dec.next(line), ipc::LineDecoder::Status::NeedMore);
    dec.feed(":2}\n");
    ASSERT_EQ(dec.next(line), ipc::LineDecoder::Status::Line);
    EXPECT_TRUE(ipc::parse(line).has_value());

    ipc::LineDecoder big;
    big.feed(QByteArray(ipc::kMaxLineBytes + 10, 'x'));
    EXPECT_EQ(big.next(line), ipc::LineDecoder::Status::Oversized);
}

TEST(IpcLines, MessagesHaveStableShape)
{
    const auto req = ipc::makeRequest(7, QStringLiteral("voice.join"), {{"channel", "1"}});
    EXPECT_EQ(req.value("v").toInt(), 1);
    EXPECT_EQ(req.value("id").toInteger(), 7);
    const auto err = ipc::makeError(7, ipc::errors::PermissionDenied, QStringLiteral("no"));
    EXPECT_FALSE(err.value("ok").toBool());
    EXPECT_EQ(err.value("error").toObject().value("code").toString(), QStringLiteral("PermissionDenied"));
    EXPECT_FALSE(ipc::parse("not json").has_value());
    EXPECT_FALSE(ipc::parse("[1,2]").has_value());
}
