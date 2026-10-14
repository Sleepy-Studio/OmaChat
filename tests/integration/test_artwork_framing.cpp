#include "Harness.hpp"
#include "omachat/core/Permissions.hpp"
#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QScopeGuard>
#include <QSemaphore>
#include <QThreadPool>
#include <QTimer>
#include <gtest/gtest.h>
using namespace omachat;
using namespace omachat::test;
namespace {
// Hold preparation in the worker queue while protocol/control requests proceed.
class PreparationGate {
public:
    PreparationGate()
    {
        pool->waitForDone();
        previous = pool->maxThreadCount();
        pool->setMaxThreadCount(1);
        pool->start([this] {
            entered.release();
            release.acquire();
        });
    }
    ~PreparationGate()
    {
        unblock();
        pool->waitForDone();
        pool->setMaxThreadCount(previous);
    }
    bool ready() { return entered.tryAcquire(1, 5000); }
    void unblock()
    {
        if (!released) {
            released = true;
            release.release();
        }
    }

private:
    QThreadPool* pool = QThreadPool::globalInstance();
    int previous = 0;
    bool released = false;
    QSemaphore entered, release;
};

class ArtworkFraming : public ::testing::Test {
protected:
    TestServer server;
    TestDaemon owner{"framing-owner"}, member{"framing-member"};
    QTemporaryDir files;
    QString sid, cid;
    void SetUp() override
    {
        ASSERT_TRUE(server.start());
        ASSERT_TRUE(owner.start());
        ASSERT_TRUE(member.start());
        ASSERT_TRUE(owner.registerOn(server, "framing-owner", "test-password"));
        ASSERT_TRUE(member.registerOn(server, "framing-member", "test-password"));
        const auto community = owner.call("server.create", {{"name", "Artwork"}});
        ASSERT_TRUE(community.ok);
        sid = community.result.value("id").toString();
        const auto invite = owner.call("invite.create", {{"server", sid}});
        ASSERT_TRUE(invite.ok);
        ASSERT_TRUE(member.call("server.join", {{"invite", invite.result.value("token")}}).ok);
        const auto channel = owner.call("channel.create", {{"server", sid}, {"name", "Artwork channel"}});
        ASSERT_TRUE(channel.ok);
        cid = channel.result.value("id").toString();
        QImage image(100, 50, QImage::Format_RGB32);
        image.fill(Qt::red);
        for (int y = 0; y < 50; ++y)
            for (int x = 50; x < 100; ++x)
                image.setPixelColor(x, y, Qt::blue);
        image.setText("Description", "private image description");
        ASSERT_TRUE(image.save(files.filePath("image.png")));
    }
    QJsonObject framing() const
    {
        return {{"source_width", 100}, {"source_height", 50}, {"x", 50}, {"y", 0}, {"width", 50}, {"height", 50}};
    }
    ipc::Reply upload(const QJsonObject& f, bool framed = true)
    {
        QJsonObject p{{"channel", cid}, {"kind", "icon"}, {"file", files.filePath("image.png")}};
        if (framed)
            p.insert("framing", f);
        return owner.call("channel.artwork.set", p);
    }
    ipc::Reply download(TestDaemon& daemon, const QString& id, const QString& variant, const QString& name)
    {
        return daemon.call(
            "attachment.download", {{"attachment", id}, {"artwork_variant", variant}, {"to", files.filePath(name)}});
    }
    std::uint64_t beginRawUpload(RawClient& raw)
    {
        QFile image(files.filePath("image.png"));
        if (!image.open(QIODevice::ReadOnly))
            return 0;
        const auto bytes = image.readAll();
        proto::Envelope request;
        auto* begin = request.mutable_begin_upload();
        begin->set_channel_id(cid.toULongLong());
        begin->set_channel_artwork(true);
        begin->set_filename("image.png");
        begin->set_size(static_cast<uint64_t>(bytes.size()));
        auto* f = begin->mutable_artwork_framing();
        f->set_kind(proto::ArtworkFraming::ICON);
        f->set_source_width(100);
        f->set_source_height(50);
        f->set_x(50);
        f->set_width(50);
        f->set_height(50);
        auto reply = raw.call(request);
        if (!reply || !reply->has_upload_ticket())
            return 0;
        const auto id = reply->upload_ticket().attachment_id();
        request.Clear();
        request.mutable_upload_chunk()->set_attachment_id(id);
        request.mutable_upload_chunk()->set_data(bytes.toStdString());
        reply = raw.call(request);
        return reply && reply->has_ok() ? id : 0;
    }
};
} // namespace
TEST_F(ArtworkFraming, CancelDuringPreparationCannotPublishLateVariants)
{
    RawClient raw(server);
    ASSERT_TRUE(raw.connect());
    ASSERT_TRUE(raw.hello());
    ASSERT_TRUE(raw.login("framing-owner", "test-password"));
    const auto id = beginRawUpload(raw);
    ASSERT_NE(id, 0u);
    PreparationGate gate;
    ASSERT_TRUE(gate.ready());
    QTimer::singleShot(30, [&] {
        proto::Envelope request;
        request.mutable_finish_upload()->set_attachment_id(id);
        const auto busy = raw.call(request);
        EXPECT_TRUE(busy && busy->has_error() && busy->error().code() == proto::ERROR_RATE_LIMITED);
        request.Clear();
        request.mutable_cancel_upload()->set_attachment_id(id);
        const auto cancelled = raw.call(request);
        EXPECT_TRUE(cancelled && cancelled->has_ok());
        gate.unblock();
    });
    proto::Envelope request;
    request.mutable_finish_upload()->set_attachment_id(id);
    EXPECT_FALSE(raw.call(request, 300));
    QThreadPool::globalInstance()->waitForDone();
    for (const QString& variant : {QString("full"), QString("small"), QString("source")})
        EXPECT_FALSE(download(owner, QString::number(id), variant, "cancelled-" + variant).ok);
    const auto retry = raw.call(request);
    ASSERT_TRUE(retry && retry->has_error());
    EXPECT_EQ(retry->error().code(), proto::ERROR_NOT_FOUND);
}

TEST_F(ArtworkFraming, ReconnectDuringPreparationRequiresExplicitFinishOnNewConnection)
{
    RawClient raw(server), resumed(server);
    ASSERT_TRUE(raw.connect());
    ASSERT_TRUE(raw.hello());
    ASSERT_TRUE(raw.login("framing-owner", "test-password"));
    ASSERT_TRUE(resumed.connect());
    ASSERT_TRUE(resumed.hello());
    ASSERT_TRUE(resumed.login("framing-owner", "test-password"));
    const auto id = beginRawUpload(raw);
    ASSERT_NE(id, 0u);
    PreparationGate gate;
    ASSERT_TRUE(gate.ready());
    QTimer::singleShot(30, [&] {
        proto::Envelope request;
        request.mutable_finish_upload()->set_attachment_id(id);
        const auto busy = raw.call(request);
        EXPECT_TRUE(busy && busy->has_error() && busy->error().code() == proto::ERROR_RATE_LIMITED);
        raw.abort();
        request.Clear();
        request.mutable_resume_upload()->set_attachment_id(id);
        const auto ticket = resumed.call(request);
        EXPECT_TRUE(ticket && ticket->has_upload_ticket());
        gate.unblock();
    });
    proto::Envelope request;
    request.mutable_finish_upload()->set_attachment_id(id);
    EXPECT_FALSE(raw.call(request, 1000));
    QThreadPool::globalInstance()->waitForDone();
    // Download pumps the completion callback: it must not publish for the old session.
    EXPECT_FALSE(download(owner, QString::number(id), "source", "before-explicit-finish").ok);
    const auto finished = resumed.call(request);
    ASSERT_TRUE(finished && finished->has_attachment());
    EXPECT_EQ(finished->attachment().id(), id);
    EXPECT_TRUE(download(owner, QString::number(id), "source", "resumed-source").ok);
}

TEST_F(ArtworkFraming, RevocationDuringPreparationAbortsPublication)
{
    const auto self = member.call("state.snapshot").result.value("self").toObject().value("id");
    ASSERT_TRUE(owner
            .call("override.set",
                {{"channel", cid}, {"user", self}, {"allow", QJsonArray{"MANAGE_CHANNEL"}}, {"deny", QJsonArray{}}})
            .ok);
    RawClient raw(server);
    ASSERT_TRUE(raw.connect());
    ASSERT_TRUE(raw.hello());
    ASSERT_TRUE(raw.login("framing-member", "test-password"));
    const auto id = beginRawUpload(raw);
    ASSERT_NE(id, 0u);
    PreparationGate gate;
    ASSERT_TRUE(gate.ready());
    QTimer::singleShot(30, [&] {
        proto::Envelope request;
        request.mutable_finish_upload()->set_attachment_id(id);
        const auto busy = raw.call(request);
        EXPECT_TRUE(busy && busy->has_error() && busy->error().code() == proto::ERROR_RATE_LIMITED);
        EXPECT_TRUE(owner
                .call("override.set",
                    {{"channel", cid}, {"user", self}, {"allow", QJsonArray{}}, {"deny", QJsonArray{"MANAGE_CHANNEL"}}})
                .ok);
        gate.unblock();
    });
    proto::Envelope request;
    request.mutable_finish_upload()->set_attachment_id(id);
    const auto denied = raw.call(request);
    ASSERT_TRUE(denied && denied->has_error());
    EXPECT_EQ(denied->error().code(), proto::ERROR_PERMISSION_DENIED);
    for (const QString& variant : {QString("full"), QString("small"), QString("source")})
        EXPECT_FALSE(download(owner, QString::number(id), variant, "revoked-preparation-" + variant).ok);
    const auto retry = raw.call(request);
    ASSERT_TRUE(retry && retry->has_error());
    EXPECT_EQ(retry->error().code(), proto::ERROR_NOT_FOUND);
}

TEST_F(ArtworkFraming, DownloadsVariantsPersistsSourceAcrossServerRestartAndRestrictsManagers)
{
    const auto updated = upload(framing());
    ASSERT_TRUE(updated.ok) << updated.errorMessage.toStdString();
    const auto id = updated.result.value("icon_attachment_id").toString();
    ASSERT_NE(id, "0");
    ASSERT_TRUE(waitFor([&] { return member.call("state.snapshot").result.value("channels").toArray().size() >= 2; }));
    auto full = download(member, id, "full", "full");
    ASSERT_TRUE(full.ok) << full.errorMessage.toStdString();
    EXPECT_EQ(QImage(full.result.value("path").toString()).size(), QSize(512, 512));
    auto small = download(member, id, "small", "small");
    ASSERT_TRUE(small.ok) << small.errorMessage.toStdString();
    EXPECT_EQ(QImage(small.result.value("path").toString()).size(), QSize(128, 128));
    EXPECT_EQ(small.result.value("framing").toObject().value("x").toInt(), 50);
    EXPECT_FALSE(download(member, id, "source", "denied").ok);
    auto source = download(owner, id, "source", "source");
    ASSERT_TRUE(source.ok) << source.errorMessage.toStdString();
    EXPECT_EQ(QImage(source.result.value("path").toString()).size(), QSize(100, 50));
    EXPECT_TRUE(QImage(source.result.value("path").toString()).textKeys().isEmpty());
    server.stop();
    ASSERT_TRUE(server.start());
    TestDaemon restarted{"framing-restarted"};
    ASSERT_TRUE(restarted.start());
    ASSERT_TRUE(restarted.loginOn(server, "framing-owner", "test-password"));
    source = download(restarted, id, "source", "source-after-restart");
    ASSERT_TRUE(source.ok) << source.errorMessage.toStdString();
    EXPECT_EQ(source.result.value("framing").toObject().value("x").toInt(), 50);
    EXPECT_EQ(QImage(source.result.value("path").toString()).size(), QSize(100, 50));
}
TEST_F(ArtworkFraming, LegacyAssetsFallBackAndMalformedFramingNeverReplacesArtwork)
{
    const auto legacy = upload({}, false);
    ASSERT_TRUE(legacy.ok);
    const auto id = legacy.result.value("icon_attachment_id").toString();
    auto small = download(owner, id, "small", "legacy-small");
    ASSERT_TRUE(small.ok);
    EXPECT_EQ(QImage(small.result.value("path").toString()).size(), QSize(100, 50));
    EXPECT_FALSE(download(owner, id, "source", "legacy-source").ok);
    for (const auto& field : {"x", "width", "source_width"}) {
        auto f = framing();
        f.insert(field, 8193);
        EXPECT_FALSE(upload(f).ok);
    }
    auto f = framing();
    f.insert("x", 0.25);
    EXPECT_FALSE(upload(f).ok);
    f = framing();
    f.insert("x", 51);
    EXPECT_FALSE(upload(f).ok);
    f = framing();
    f.insert("source_width", 101);
    EXPECT_FALSE(upload(f).ok);
    const auto channel = owner.call("channel.details.reload", {{"channel", cid}});
    ASSERT_TRUE(channel.ok);
    EXPECT_EQ(channel.result.value("icon_attachment_id").toString(), id);
    EXPECT_FALSE(download(owner, id, "invalid", "invalid").ok);
}
TEST_F(ArtworkFraming, CopiedArtworkSurvivesOriginalDeletionAndRevocationAppliesToAllVariants)
{
    const auto updated = upload(framing());
    ASSERT_TRUE(updated.ok);
    const auto id = updated.result.value("icon_attachment_id").toString();
    const auto copied = owner.call("channel.duplicate", {{"channel", cid}, {"name", "Copy"}});
    ASSERT_TRUE(copied.ok) << copied.errorMessage.toStdString();
    const auto copy = copied.result.value("id").toString();
    ASSERT_TRUE(owner.call("channel.delete", {{"channel", cid}}).ok);
    EXPECT_TRUE(download(owner, id, "source", "copied-source").ok);
    const auto self = member.call("state.snapshot").result.value("self").toObject().value("id");
    ASSERT_TRUE(owner
            .call("override.set",
                {{"channel", copy}, {"user", self}, {"allow", QJsonArray{}}, {"deny", QJsonArray{"VIEW_CHANNEL"}}})
            .ok);
    EXPECT_FALSE(download(member, id, "full", "revoked-full").ok);
    EXPECT_FALSE(download(member, id, "small", "revoked-small").ok);
    EXPECT_FALSE(download(member, id, "source", "revoked-source").ok);
    EXPECT_TRUE(download(owner, id, "source", "still-owned-source").ok);
}
TEST_F(ArtworkFraming, ServerArtworkSourceRequiresServerManagement)
{
    const auto updated = owner.call("server.artwork.set",
        {{"server", sid}, {"kind", "icon"}, {"file", files.filePath("image.png")}, {"framing", framing()}});
    ASSERT_TRUE(updated.ok) << updated.errorMessage.toStdString();
    const auto id = updated.result.value("icon_attachment_id").toString();
    EXPECT_TRUE(download(owner, id, "source", "server-source").ok);
    EXPECT_TRUE(download(member, id, "small", "server-small").ok);
    EXPECT_FALSE(download(member, id, "source", "server-denied").ok);
}

TEST_F(ArtworkFraming, ArtworkPreparationDoesNotBlockControlRequests)
{
    auto* pool = QThreadPool::globalInstance();
    pool->waitForDone();
    const int previous = pool->maxThreadCount();
    pool->setMaxThreadCount(1);
    QSemaphore entered, release;
    pool->start([&] {
        entered.release();
        release.acquire();
    });
    ASSERT_TRUE(entered.tryAcquire(1, 5000));
    bool released = false;
    const auto restore = qScopeGuard([&] {
        if (!released)
            release.release();
        pool->waitForDone();
        pool->setMaxThreadCount(previous);
    });
    bool responsive = false;
    QTimer::singleShot(30, [&] {
        responsive = owner.call("channel.details.reload", {{"channel", cid}}, 2000).ok;
        released = true;
        release.release();
    });
    const auto uploaded = upload(framing());
    EXPECT_TRUE(responsive);
    ASSERT_TRUE(uploaded.ok) << uploaded.errorMessage.toStdString();
}
TEST_F(ArtworkFraming, RawFramingStripsUnknownFieldsAndCannotBeAppliedToWrongSlot)
{
    RawClient raw(server);
    ASSERT_TRUE(raw.connect());
    ASSERT_TRUE(raw.hello());
    ASSERT_TRUE(raw.login("framing-owner", "test-password"));
    QFile image(files.filePath("image.png"));
    ASSERT_TRUE(image.open(QIODevice::ReadOnly));
    const auto bytes = image.readAll();
    proto::Envelope request;
    auto* begin = request.mutable_begin_upload();
    begin->set_channel_id(cid.toULongLong());
    begin->set_channel_artwork(true);
    begin->set_filename("image.png");
    begin->set_size(static_cast<uint64_t>(bytes.size()));
    auto* f = begin->mutable_artwork_framing();
    f->set_kind(proto::ArtworkFraming::ICON);
    f->set_source_width(100);
    f->set_source_height(50);
    f->set_x(50);
    f->set_width(50);
    f->set_height(50);
    *f->mutable_unknown_fields() = std::string("\xFA\x01\x13", 3) + "unexpected metadata";
    auto reply = raw.call(request);
    ASSERT_TRUE(reply && reply->has_upload_ticket());
    const auto id = reply->upload_ticket().attachment_id();
    request.Clear();
    request.mutable_upload_chunk()->set_attachment_id(id);
    request.mutable_upload_chunk()->set_data(bytes.toStdString());
    reply = raw.call(request);
    ASSERT_TRUE(reply && reply->has_ok());
    request.Clear();
    request.mutable_finish_upload()->set_attachment_id(id);
    reply = raw.call(request);
    ASSERT_TRUE(reply && reply->has_attachment()) << (reply ? reply->error().message() : "no reply");
    EXPECT_FALSE(
        reply->attachment().artwork_framing().SerializeAsString().find("unexpected metadata") != std::string::npos);
    request.Clear();
    auto* update = request.mutable_update_channel();
    update->set_channel_id(cid.toULongLong());
    update->set_set_banner(true);
    update->set_banner_attachment_id(id);
    reply = raw.call(request);
    ASSERT_TRUE(reply && reply->has_error());
    EXPECT_EQ(reply->error().code(), proto::ERROR_BAD_REQUEST);
    request.Clear();
    request.mutable_cancel_upload()->set_attachment_id(id);
    reply = raw.call(request);
    ASSERT_TRUE(reply && reply->has_ok());
    EXPECT_FALSE(download(owner, QString::number(id), "source", "cancelled-source").ok);
}

TEST_F(ArtworkFraming, ServerArtworkSurvivesCarrierAndFinalChannelDeletion)
{
    // Make the carrier deterministic before the server upload selects it.
    const auto channels = owner.call("state.snapshot").result.value("channels").toArray();
    for (const auto& value : channels) {
        const auto channel = value.toObject();
        const auto id = channel.value("id").toString();
        if (channel.value("server_id").toString() == sid && id != cid) {
            ASSERT_TRUE(owner.call("channel.delete", {{"channel", id}}).ok);
        }
    }
    const auto updated = owner.call("server.artwork.set",
        {{"server", sid}, {"kind", "icon"}, {"file", files.filePath("image.png")}, {"framing", framing()}});
    ASSERT_TRUE(updated.ok) << updated.errorMessage.toStdString();
    const auto id = updated.result.value("icon_attachment_id").toString();
    const auto last = owner.call("channel.create", {{"server", sid}, {"name", "Last channel"}});
    ASSERT_TRUE(last.ok);
    ASSERT_TRUE(owner.call("channel.delete", {{"channel", cid}}).ok);
    const auto verify = [&](const QString& suffix) {
        for (const QString& variant : {QString("full"), QString("small"), QString("source")}) {
            const auto downloaded = download(owner, id, variant, variant + suffix);
            EXPECT_TRUE(downloaded.ok) << downloaded.errorMessage.toStdString();
        }
        EXPECT_TRUE(download(member, id, "full", "member-full" + suffix).ok);
        EXPECT_TRUE(download(member, id, "small", "member-small" + suffix).ok);
        EXPECT_FALSE(download(member, id, "source", "member-source" + suffix).ok);
    };
    verify("-after-carrier");
    ASSERT_TRUE(owner.call("channel.delete", {{"channel", last.result.value("id")}}).ok);
    verify("-after-final-channel");
    ASSERT_TRUE(member.call("server.leave", {{"server", sid}}).ok);
    for (const QString& variant : {QString("full"), QString("small"), QString("source")})
        EXPECT_FALSE(download(member, id, variant, "revoked-server-" + variant).ok);
    ASSERT_TRUE(owner.call("server.delete", {{"server", sid}}).ok);
    for (const QString& variant : {QString("full"), QString("small"), QString("source")})
        EXPECT_FALSE(download(owner, id, variant, "deleted-server-" + variant).ok);
}
