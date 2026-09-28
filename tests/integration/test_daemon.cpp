// omachatd end-to-end over its real IPC socket: certificate trust,
// accounts, messaging between two daemons, reconnection and persistence of
// the session independently of any GUI client.

#include "Harness.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace omachat;
using namespace omachat::test;

namespace {

struct DaemonFixture : ::testing::Test {
    TestServer server;
    std::unique_ptr<TestDaemon> alice;
    std::unique_ptr<TestDaemon> bob;

    void SetUp() override
    {
        ASSERT_TRUE(server.start());
        alice = std::make_unique<TestDaemon>(QStringLiteral("alice"));
        bob = std::make_unique<TestDaemon>(QStringLiteral("bob"));
        ASSERT_TRUE(alice->start());
        ASSERT_TRUE(bob->start());
    }

    void TearDown() override
    {
        alice.reset();
        bob.reset();
    }

    // alice owns "Sleepy Studio", bob joined through an invite.
    void setupPair()
    {
        ASSERT_TRUE(alice->registerOn(server, QStringLiteral("alice"), QStringLiteral("alice-password")));
        ASSERT_TRUE(bob->registerOn(server, QStringLiteral("bob"), QStringLiteral("bob-password")));
        auto created = alice->call(QStringLiteral("server.create"), {{"name", "Sleepy Studio"}});
        ASSERT_TRUE(created.ok) << created.errorMessage.toStdString();
        auto inv = alice->call(QStringLiteral("invite.create"), {{"server", "Sleepy Studio"}});
        ASSERT_TRUE(inv.ok);
        EXPECT_TRUE(inv.result.value("uri").toString().startsWith("omachat://invite/"));
        auto joined = bob->call(QStringLiteral("server.join"), {{"invite", inv.result.value("uri")}});
        ASSERT_TRUE(joined.ok) << joined.errorMessage.toStdString();
        ASSERT_TRUE(waitFor([&] {
            return alice->call(QStringLiteral("member.list"), {{"server", "Sleepy Studio"}})
                       .result.value("members")
                       .toArray()
                       .size()
                == 2;
        }));
    }
};

} // namespace

TEST_F(DaemonFixture, NotConfiguredStateIsExplicit)
{
    auto st = alice->call(QStringLiteral("daemon.status"));
    ASSERT_TRUE(st.ok);
    EXPECT_EQ(st.result.value("state").toString(), QStringLiteral("not_configured"));
    EXPECT_FALSE(st.result.value("connected").toBool());
    EXPECT_TRUE(st.result.value("account").isNull());
    auto list = alice->call(QStringLiteral("server.list"));
    EXPECT_FALSE(list.ok);
    EXPECT_EQ(list.errorCode, QStringLiteral("NotConnected"));
    EXPECT_EQ(alice->call(QStringLiteral("no.such.method")).errorCode, QStringLiteral("UnknownMethod"));
}

TEST_F(DaemonFixture, UntrustedCertificateIsNeverSilentlyAccepted)
{
    auto r = alice->call(QStringLiteral("account.register"),
        {{"host", "127.0.0.1"}, {"port", server.port()}, {"username", "alice"}, {"password", "pw123456"}});
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.errorCode, QStringLiteral("CertificateError"));
    const auto st = alice->call(QStringLiteral("daemon.status")).result;
    EXPECT_EQ(st.value("state").toString(), QStringLiteral("error"));
    EXPECT_EQ(st.value("error").toObject().value("fingerprint").toString(), server.fingerprint());
    // A different fingerprint than the one presented is refused.
    auto wrong = alice->call(QStringLiteral("certificate.trust"), {{"fingerprint", "SHA256:00:11"}});
    EXPECT_EQ(wrong.errorCode, QStringLiteral("CertificateError"));
}

TEST_F(DaemonFixture, MessagesFlowBetweenDaemonsWithEvents)
{
    setupPair();
    auto sent = alice->call(
        QStringLiteral("message.send"), {{"channel", "general"}, {"content", "hello @bob from **alice**"}});
    ASSERT_TRUE(sent.ok) << sent.errorMessage.toStdString();
    auto ev = bob->waitEvent(QStringLiteral("message.created"));
    ASSERT_TRUE(ev);
    EXPECT_EQ(ev->value("content").toString(), QStringLiteral("hello @bob from **alice**"));
    EXPECT_TRUE(ev->value("mentions_me").toBool());

    auto hist = bob->call(QStringLiteral("message.history"), {{"channel", "general"}});
    ASSERT_TRUE(hist.ok);
    EXPECT_EQ(hist.result.value("messages").toArray().size(), 1);

    auto dm = bob->call(QStringLiteral("dm.open"), {{"user", "alice"}});
    ASSERT_TRUE(dm.ok);
    ASSERT_TRUE(
        bob->call(QStringLiteral("message.send"), {{"channel", dm.result.value("id")}, {"content", "psst"}}).ok);
    auto dmEv = alice->waitEvent(QStringLiteral("message.created"),
        [](const QJsonObject& m) { return m.value("content").toString() == u"psst"; });
    EXPECT_TRUE(dmEv);

    ASSERT_TRUE(alice->call(QStringLiteral("presence.set"), {{"status", "dnd"}}).ok);
    auto pres = bob->waitEvent(
        QStringLiteral("presence"), [](const QJsonObject& p) { return p.value("status").toString() == u"dnd"; });
    EXPECT_TRUE(pres);
}

TEST_F(DaemonFixture, SessionSurvivesIpcClientDisconnect)
{
    setupPair();
    ASSERT_TRUE(alice->call(QStringLiteral("voice.join"), {{"channel", "General"}}).ok);
    // The GUI-like IPC client closes: the daemon keeps the session.
    alice->ipc().disconnectFromDaemon();
    ASSERT_TRUE(waitFor([&] { return !alice->ipc().isConnected(); }));
    waitFor([] { return false; }, 300);
    const auto st = alice->daemon().statusJson();
    EXPECT_EQ(st.value("state").toString(), QStringLiteral("connected"));
    EXPECT_TRUE(st.value("voice").toObject().value("joined").toBool());
}

TEST_F(DaemonFixture, PushToTalkIsReleasedWhenItsClientVanishes)
{
    setupPair();
    ASSERT_TRUE(alice->call(QStringLiteral("voice.mode"), {{"mode", "ptt"}}).ok);
    ASSERT_TRUE(alice->call(QStringLiteral("ptt.begin")).ok);
    EXPECT_TRUE(alice->daemon().statusJson().value("voice").toObject().value("ptt").toBool());
    alice->ipc().disconnectFromDaemon();
    ASSERT_TRUE(waitFor([&] { return !alice->daemon().statusJson().value("voice").toObject().value("ptt").toBool(); }));
}

TEST_F(DaemonFixture, ReconnectsAndResynchronizesAfterServerRestart)
{
    setupPair();
    const quint16 port = server.port();
    const quint16 media = server.mediaPort();
    ASSERT_TRUE(alice->call(QStringLiteral("voice.join"), {{"channel", "General"}}).ok);

    server.stop();
    ASSERT_TRUE(waitFor(
        [&] {
            const auto s = alice->daemon().statusJson().value("state").toString();
            return s == u"reconnecting" || s == u"offline";
        },
        5000));
    // The UI keeps the last model instead of going blank.
    EXPECT_TRUE(alice->call(QStringLiteral("state.snapshot")).result.value("valid").toBool());

    ASSERT_TRUE(server.start(port, media));
    // Access tokens died with the old process: the daemon falls back to its
    // refresh token, then fully resynchronizes and rejoins voice.
    ASSERT_TRUE(alice->waitState(QStringLiteral("connected"), 40000));
    ASSERT_TRUE(waitFor(
        [&] {
            return alice->daemon().statusJson().value("voice").toObject().value("joined").toBool()
                && alice->daemon().connection().model().voiceState(alice->daemon().connection().model().self().id());
        },
        10000));
    auto sent = alice->call(QStringLiteral("message.send"), {{"channel", "general"}, {"content", "back online"}});
    EXPECT_TRUE(sent.ok) << sent.errorMessage.toStdString();
}

TEST_F(DaemonFixture, StatusJsonContractIsStable)
{
    setupPair();
    ASSERT_TRUE(alice->call(QStringLiteral("voice.join"), {{"channel", "General"}}).ok);
    const auto s = alice->call(QStringLiteral("daemon.status")).result;
    for (const char* key : {"version", "connected", "state", "error", "account", "instance", "user", "server", "voice",
             "audio", "clients"})
        EXPECT_TRUE(s.contains(QLatin1StringView(key))) << key;
    const auto voice = s.value("voice").toObject();
    for (const char* key : {"joined", "channel", "channel_id", "muted", "deafened", "mode", "ptt", "transmitting",
             "participants", "count"})
        EXPECT_TRUE(voice.contains(QLatin1StringView(key))) << key;
    EXPECT_EQ(voice.value("channel").toString(), QStringLiteral("General"));
    EXPECT_EQ(s.value("server").toObject().value("name").toString(), QStringLiteral("Sleepy Studio"));
    EXPECT_TRUE(s.value("server").toObject().value("id").isString()) << "64-bit ids travel as strings";
}

TEST_F(DaemonFixture, MuteAndDeafenPropagateToOtherMembers)
{
    setupPair();
    ASSERT_TRUE(alice->call(QStringLiteral("voice.join"), {{"channel", "General"}}).ok);
    ASSERT_TRUE(bob->call(QStringLiteral("voice.join"), {{"channel", "General"}}).ok);
    auto r = alice->call(QStringLiteral("voice.deafen"));
    ASSERT_TRUE(r.ok);
    EXPECT_TRUE(r.result.value("deafened").toBool());
    auto ev = bob->waitEvent(QStringLiteral("voice.state"),
        [](const QJsonObject& v) { return v.value("self_deaf").toBool() && v.value("self_mute").toBool(); });
    EXPECT_TRUE(ev) << "deafen implies mute and is visible to others";
    auto un = alice->call(QStringLiteral("voice.unmute"));
    EXPECT_FALSE(un.result.value("deafened").toBool()) << "unmuting also undeafens";
}

TEST_F(DaemonFixture, AttachmentsTravelBetweenDaemons)
{
    setupPair();
    QTemporaryDir files;
    const qint64 size = 1300 * 1024; // three chunks
    QByteArray data;
    for (qint64 i = 0; i < size; ++i)
        data.append(static_cast<char>((i * 31 + i / 7) & 0xff));
    const QString source = files.filePath(QStringLiteral("capture.png"));
    {
        QFile f(source);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(data);
    }

    auto sent = alice->call(QStringLiteral("message.send"),
        {{"channel", "general"}, {"content", "look"}, {"files", QJsonArray{source}}});
    ASSERT_TRUE(sent.ok) << sent.errorMessage.toStdString();
    const auto att = sent.result.value("attachments").toArray();
    ASSERT_EQ(att.size(), 1);
    EXPECT_EQ(att[0].toObject().value("filename").toString(), QStringLiteral("capture.png"));
    EXPECT_EQ(att[0].toObject().value("size").toDouble(), static_cast<double>(size));
    EXPECT_EQ(att[0].toObject().value("mime_type").toString(), QStringLiteral("image/png"));

    auto ev = bob->waitEvent(QStringLiteral("message.created"));
    ASSERT_TRUE(ev);
    const QJsonObject a = ev->value("attachments").toArray().at(0).toObject();
    ASSERT_FALSE(a.isEmpty());

    // Explicit directory: the original name is kept and never overwritten.
    QTemporaryDir out;
    auto got = bob->call(QStringLiteral("attachment.download"),
        {{"attachment", a.value("id")}, {"filename", a.value("filename")}, {"to", out.path()}});
    ASSERT_TRUE(got.ok) << got.errorMessage.toStdString();
    EXPECT_EQ(got.result.value("path").toString(), out.filePath(QStringLiteral("capture.png")));
    auto again = bob->call(QStringLiteral("attachment.download"),
        {{"attachment", a.value("id")}, {"filename", a.value("filename")}, {"to", out.path()}});
    ASSERT_TRUE(again.ok);
    EXPECT_EQ(again.result.value("path").toString(), out.filePath(QStringLiteral("capture (1).png")));
    QFile f(got.result.value("path").toString());
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    EXPECT_EQ(f.readAll(), data);
    EXPECT_FALSE(QFileInfo::exists(got.result.value("path").toString() + QStringLiteral(".part")));

    // The cache is reused once the file is there.
    QTemporaryDir cache;
    qputenv("XDG_CACHE_HOME", cache.path().toLocal8Bit());
    const QJsonObject cacheParams{
        {"attachment", a.value("id")}, {"filename", "../escape.png"}, {"to", "cache"}, {"size", a.value("size")}};
    auto first = bob->call(QStringLiteral("attachment.download"), cacheParams);
    ASSERT_TRUE(first.ok) << first.errorMessage.toStdString();
    EXPECT_TRUE(first.result.value("path").toString().startsWith(cache.path()));
    EXPECT_FALSE(first.result.value("path").toString().contains(QStringLiteral("/../")));
    EXPECT_FALSE(first.result.value("cached").toBool());
    auto second = bob->call(QStringLiteral("attachment.download"), cacheParams);
    EXPECT_TRUE(second.result.value("cached").toBool());
    qunsetenv("XDG_CACHE_HOME");

    // Local limits are reported before any bytes move.
    auto missing = alice->call(QStringLiteral("message.send"),
        {{"channel", "general"}, {"files", QJsonArray{files.filePath(QStringLiteral("nope"))}}});
    EXPECT_EQ(missing.errorCode, QStringLiteral("NotFound"));
}
