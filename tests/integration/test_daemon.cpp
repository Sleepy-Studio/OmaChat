// omachatd end-to-end over its real IPC socket: certificate trust,
// accounts, messaging between two daemons, reconnection and persistence of
// the session independently of any GUI client.

#include "Harness.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
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

TEST_F(DaemonFixture, CreateServerFromDiscordExport)
{
    ASSERT_TRUE(alice->registerOn(server, QStringLiteral("alice"), QStringLiteral("alice-password")));
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString assetPath = dir.filePath(QStringLiteral("photo.txt"));
    QFile asset(assetPath);
    ASSERT_TRUE(asset.open(QIODevice::WriteOnly));
    ASSERT_EQ(asset.write("archived attachment"), 19);
    asset.close();
    const QString exportPath = dir.filePath(QStringLiteral("channel.json"));
    QJsonObject message{{"id", "100"}, {"timestamp", "2020-01-01T00:00:00.000Z"},
        {"content", "old message"}, {"author", QJsonObject{{"id", "111"}, {"name", "Former member"}}},
        {"attachments", QJsonArray{QJsonObject{{"url", "photo.txt"}, {"fileName", "photo.txt"}}}}};
    QJsonObject data{{"guild", QJsonObject{{"id", "999"}}},
        {"channel", QJsonObject{{"id", "888"}, {"name", "old-chat"}}},
        {"messages", QJsonArray{message}}};
    QFile exportFile(exportPath);
    ASSERT_TRUE(exportFile.open(QIODevice::WriteOnly));
    QJsonObject missingMessage = message;
    missingMessage.insert("attachments", QJsonArray{QJsonObject{{"url", "missing.txt"}, {"fileName", "missing.txt"}}});
    data["messages"] = QJsonArray{missingMessage};
    exportFile.write(QJsonDocument(data).toJson());
    exportFile.close();
    const QJsonObject params{{"name", "Imported place"},
        {"files", QJsonArray{QUrl::fromLocalFile(exportPath).toString()}}};
    const auto refused = alice->call(QStringLiteral("server.create_from_discord"), params);
    EXPECT_FALSE(refused.ok);
    EXPECT_TRUE(alice->call(QStringLiteral("server.list")).result.value("servers").toArray().isEmpty());

    ASSERT_TRUE(exportFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    data["messages"] = QJsonArray{message};
    exportFile.write(QJsonDocument(data).toJson());
    exportFile.close();

    const auto created = alice->call(QStringLiteral("server.create_from_discord"), params, 30000);
    ASSERT_TRUE(created.ok) << created.errorMessage.toStdString();
    EXPECT_EQ(created.result.value("imported").toInt(), 1);
    const auto channels = alice->call(QStringLiteral("channel.list"), {{"server", created.result.value("id")}});
    ASSERT_TRUE(channels.ok);
    QString importedId;
    for (const auto& value : channels.result.value("channels").toArray()) {
        const auto channel = value.toObject();
        if (channel.value("name").toString() == u"old-chat")
            importedId = channel.value("id").toString();
    }
    ASSERT_FALSE(importedId.isEmpty());
    const auto history = alice->call(QStringLiteral("message.history"), {{"channel", importedId}});
    ASSERT_TRUE(history.ok);
    const auto messages = history.result.value("messages").toArray();
    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages[0].toObject().value("timestamp").toDouble(), 1577836800000.0);
    EXPECT_EQ(messages[0].toObject().value("attachments").toArray().size(), 1);
}

TEST_F(DaemonFixture, ProfileUpdateFlowsThroughIpcAndRefreshesSelf)
{
    setupPair();
    auto result = alice->call(QStringLiteral("profile.update"),
        {{"display_name", "Alice New"}, {"avatar_url", "https://example.org/alice.png"},
            {"bio", "Building OmaChat"}});
    ASSERT_TRUE(result.ok) << result.errorMessage.toStdString();
    auto event = bob->waitEvent(QStringLiteral("user.updated"), [](const QJsonObject& user) {
        return user.value("username").toString() == u"alice"
            && user.value("display_name").toString() == u"Alice New";
    });
    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(event->value("avatar_url").toString(), QStringLiteral("https://example.org/alice.png"));
    EXPECT_EQ(event->value("bio").toString(), QStringLiteral("Building OmaChat"));
    ASSERT_TRUE(waitFor([&] {
        return alice->call(QStringLiteral("daemon.status"))
                   .result.value("user").toObject().value("display_name").toString() == u"Alice New";
    }));
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

    auto sent = alice->call(
        QStringLiteral("message.send"), {{"channel", "general"}, {"content", "look"}, {"files", QJsonArray{source}}});
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

TEST_F(DaemonFixture, RoleEditsKeepOmittedFieldsAndOverridesAcceptUsernames)
{
    setupPair();
    auto created = alice->call(QStringLiteral("role.create"),
        {{"server", "Sleepy Studio"}, {"name", "DJ"}, {"permissions", QJsonArray{"SPEAK", "STREAM"}}});
    ASSERT_TRUE(created.ok) << created.errorMessage.toStdString();
    const QString roleId = created.result.value("id").toString();
    ASSERT_TRUE(waitFor([&] {
        const auto roles = alice->call(QStringLiteral("state.snapshot")).result.value("roles").toArray();
        return std::ranges::any_of(roles, [&](const QJsonValue& r) { return r.toObject().value("id") == roleId; });
    }));

    auto renamed = alice->call(QStringLiteral("role.update"), {{"role", roleId}, {"name", "Disc Jockey"}});
    ASSERT_TRUE(renamed.ok) << renamed.errorMessage.toStdString();
    EXPECT_EQ(renamed.result.value("name").toString(), QStringLiteral("Disc Jockey"));
    EXPECT_EQ(renamed.result.value("permissions").toArray(), (QJsonArray{"SPEAK", "STREAM"}))
        << "permissions left out are kept";

    auto recolored = alice->call(QStringLiteral("role.update"), {{"role", roleId}, {"color", "#61afef"}});
    ASSERT_TRUE(recolored.ok);
    EXPECT_EQ(recolored.result.value("name").toString(), QStringLiteral("Disc Jockey"));
    EXPECT_EQ(recolored.result.value("color").toString(), QStringLiteral("#61afef"));

    ASSERT_TRUE(alice
            ->call(QStringLiteral("override.set"),
                {{"channel", "general"}, {"user", "bob"}, {"deny", QJsonArray{"SEND_MESSAGES"}}})
            .ok);
    auto list = alice->call(QStringLiteral("override.list"), {{"channel", "general"}});
    ASSERT_TRUE(list.ok);
    const auto overrides = list.result.value("overrides").toArray();
    ASSERT_EQ(overrides.size(), 1);
    EXPECT_EQ(overrides.at(0).toObject().value("target_type").toString(), QStringLiteral("user"));
    EXPECT_EQ(overrides.at(0).toObject().value("deny").toArray(), (QJsonArray{"SEND_MESSAGES"}));
    EXPECT_FALSE(bob->call(QStringLiteral("message.send"), {{"channel", "general"}, {"content", "hi"}}).ok);
}

TEST_F(DaemonFixture, GroupConversationsThroughTheDaemon)
{
    setupPair();
    TestDaemon carol(QStringLiteral("carol"));
    ASSERT_TRUE(carol.start());
    ASSERT_TRUE(carol.registerOn(server, QStringLiteral("carol"), QStringLiteral("carol-password")));
    auto inv = alice->call(QStringLiteral("invite.create"), {{"server", "Sleepy Studio"}});
    ASSERT_TRUE(carol.call(QStringLiteral("server.join"), {{"invite", inv.result.value("uri")}}).ok);
    ASSERT_TRUE(waitFor([&] {
        return alice->call(QStringLiteral("member.list"), {{"server", "Sleepy Studio"}})
                   .result.value("members")
                   .toArray()
                   .size()
            == 3;
    }));

    auto made = alice->call(QStringLiteral("dm.create"), {{"users", QJsonArray{"bob", "carol"}}});
    ASSERT_TRUE(made.ok) << made.errorMessage.toStdString();
    EXPECT_EQ(made.result.value("type").toString(), QStringLiteral("group_dm"));
    EXPECT_EQ(made.result.value("recipients").toArray().size(), 3);
    const QString id = made.result.value("id").toString();
    EXPECT_FALSE(made.result.value("name").toString().isEmpty()) << "unnamed groups are named after their people";

    auto created = carol.waitEvent(
        QStringLiteral("channel.created"), [&](const QJsonObject& c) { return c.value("id").toString() == id; });
    ASSERT_TRUE(created);
    ASSERT_TRUE(carol.call(QStringLiteral("message.send"), {{"channel", id}, {"content", "hello all"}}).ok);
    EXPECT_TRUE(bob->waitEvent(QStringLiteral("message.created"),
        [](const QJsonObject& m) { return m.value("content").toString() == u"hello all"; }));

    auto renamed = bob->call(QStringLiteral("channel.update"), {{"channel", id}, {"name", "Trio"}});
    ASSERT_TRUE(renamed.ok) << renamed.errorMessage.toStdString();
    EXPECT_TRUE(alice->waitEvent(
        QStringLiteral("channel.updated"), [](const QJsonObject& c) { return c.value("name").toString() == u"Trio"; }));

    ASSERT_TRUE(carol.call(QStringLiteral("dm.leave"), {{"channel", id}}).ok);
    EXPECT_TRUE(carol.waitEvent(QStringLiteral("channel.deleted")));
    EXPECT_TRUE(alice->waitEvent(QStringLiteral("channel.updated"),
        [](const QJsonObject& c) { return c.value("recipients").toArray().size() == 2; }));
}

TEST_F(DaemonFixture, UploadsContinueAfterTheConnectionDrops)
{
    setupPair();
    QTemporaryDir files;
    const qint64 size = 24 * 1024 * 1024; // 48 chunks: still going when the link drops
    QByteArray data(size, Qt::Uninitialized);
    for (qint64 i = 0; i < size; ++i)
        data[i] = static_cast<char>((i * 131 + i / 511) & 0xff);
    const QString source = files.filePath(QStringLiteral("big.bin"));
    {
        QFile f(source);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(data);
    }

    std::optional<ipc::Reply> result;
    alice->ipc().request(
        QStringLiteral("message.send"), {{"channel", "general"}, {"content", "big one"}, {"files", QJsonArray{source}}},
        [&](const ipc::Reply& r) { result = r; }, 0);
    // Once the server has handed out an attachment id, cut the link.
    ASSERT_TRUE(alice->waitEvent(QStringLiteral("transfer.progress"),
        [](const QJsonObject& p) { return p.value("attachment_id").toString() != u"0" && !p.contains("complete"); }));
    alice->daemon().connection().dropLink(QStringLiteral("test: link dropped"));
    EXPECT_TRUE(alice->waitEvent(
        QStringLiteral("transfer.progress"), [](const QJsonObject& p) { return p.value("waiting").toBool(); }));

    ASSERT_TRUE(waitFor([&] { return result.has_value(); }, 60000));
    ASSERT_TRUE(result->ok) << result->errorMessage.toStdString();
    const auto att = result->result.value("attachments").toArray();
    ASSERT_EQ(att.size(), 1);
    EXPECT_EQ(att[0].toObject().value("size").toDouble(), static_cast<double>(size));

    // Downloads continue from what is already on disk.
    QTemporaryDir out;
    std::optional<ipc::Reply> got;
    bob->ipc().request(
        QStringLiteral("attachment.download"),
        {{"attachment", att[0].toObject().value("id")}, {"filename", "big.bin"}, {"to", out.path()}},
        [&](const ipc::Reply& r) { got = r; }, 0);
    ASSERT_TRUE(bob->waitEvent(QStringLiteral("transfer.progress"), [](const QJsonObject& p) {
        return p.value("direction").toString() == u"download" && p.value("transferred").toDouble() > 0
            && !p.contains("complete");
    }));
    bob->daemon().connection().dropLink(QStringLiteral("test: link dropped"));
    ASSERT_TRUE(waitFor([&] { return got.has_value(); }, 60000));
    ASSERT_TRUE(got->ok) << got->errorMessage.toStdString();
    QFile f(got->result.value("path").toString());
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    EXPECT_TRUE(f.readAll() == data) << "upload and download both survived a dropped link intact";
}

TEST_F(DaemonFixture, SeveralAccountsStayConnectedAndSwitchInstantly)
{
    setupPair(); // alice and bob on `server`, alice owns "Sleepy Studio"
    TestServer second;
    ASSERT_TRUE(second.start());
    ASSERT_TRUE(alice->registerOn(second, QStringLiteral("alice2"), QStringLiteral("alice-password")));
    ASSERT_TRUE(alice->call(QStringLiteral("server.create"), {{"name", "Other Place"}}).ok);

    // The second account is active; the first stays connected behind it.
    const auto accounts = alice->daemon().statusJson().value("accounts").toArray();
    ASSERT_EQ(accounts.size(), 2);
    QString firstId, secondId;
    for (const auto& v : accounts) {
        const auto a = v.toObject();
        EXPECT_EQ(a.value("state").toString(), QStringLiteral("connected"))
            << a.value("username").toString().toStdString();
        (a.value("active").toBool() ? secondId : firstId) = a.value("id").toString();
    }
    ASSERT_FALSE(firstId.isEmpty());
    ASSERT_FALSE(secondId.isEmpty());
    EXPECT_EQ(alice->daemon().statusJson().value("account").toObject().value("username").toString(),
        QStringLiteral("alice2"));

    // Activity on the background account is counted, not broadcast as messages.
    ASSERT_TRUE(bob->call(QStringLiteral("message.send"), {{"channel", "general"}, {"content", "hey @alice"}}).ok);
    auto activity = alice->waitEvent(QStringLiteral("account.activity"));
    ASSERT_TRUE(activity);
    EXPECT_EQ(activity->value("account").toString(), firstId);
    EXPECT_EQ(activity->value("unread").toInt(), 1);
    EXPECT_EQ(activity->value("mentions").toInt(), 1);

    auto switched = alice->call(QStringLiteral("account.switch"), {{"account", firstId}});
    ASSERT_TRUE(switched.ok) << switched.errorMessage.toStdString();
    EXPECT_TRUE(alice->waitEvent(QStringLiteral("state.reset")));
    const auto snap = alice->call(QStringLiteral("state.snapshot")).result;
    QStringList servers;
    for (const auto& s : snap.value("servers").toArray())
        servers << s.toObject().value("name").toString();
    EXPECT_EQ(servers, QStringList{QStringLiteral("Sleepy Studio")});
    auto hist = alice->call(QStringLiteral("message.history"), {{"channel", "general"}});
    ASSERT_TRUE(hist.ok);
    EXPECT_EQ(hist.result.value("messages").toArray().size(), 1);
    for (const auto& v : alice->daemon().statusJson().value("accounts").toArray()) {
        if (v.toObject().value("id").toString() == firstId) {
            EXPECT_EQ(v.toObject().value("unread").toInt(), 0) << "switching to an account clears its counter";
        }
    }

    // Removing the active account falls back to the other one.
    ASSERT_TRUE(alice->call(QStringLiteral("account.remove"), {{"account", firstId}}).ok);
    ASSERT_TRUE(waitFor(
        [&] { return alice->daemon().statusJson().value("account").toObject().value("id").toString() == secondId; }));
    EXPECT_EQ(alice->daemon().statusJson().value("accounts").toArray().size(), 1);
    RawClient removed(server);
    ASSERT_TRUE(removed.connect());
    ASSERT_TRUE(removed.hello());
    EXPECT_FALSE(removed.login("alice", "alice-password")) << "server account was deleted";
    EXPECT_FALSE(bob->call(QStringLiteral("server.list")).result.value("servers").toArray().size())
        << "owned server was deleted for its other members";
}

TEST_F(DaemonFixture, AccountDeletionRequiresServerConfirmation)
{
    ASSERT_TRUE(alice->registerOn(server, QStringLiteral("alice"), QStringLiteral("alice-password")));
    const auto id = alice->daemon().statusJson().value("account").toObject().value("id").toString();
    ASSERT_FALSE(id.isEmpty());
    ASSERT_TRUE(alice->call(QStringLiteral("disconnect")).ok);
    const auto removal = alice->call(QStringLiteral("account.remove"), {{"account", id}});
    EXPECT_FALSE(removal.ok);
    EXPECT_EQ(removal.errorCode, QStringLiteral("NotConnected"));
    EXPECT_EQ(alice->daemon().statusJson().value("accounts").toArray().size(), 1);

    RawClient stillRegistered(server);
    ASSERT_TRUE(stillRegistered.connect());
    ASSERT_TRUE(stillRegistered.hello());
    EXPECT_TRUE(stillRegistered.login("alice", "alice-password"));
}

TEST_F(DaemonFixture, ConversationsAreEndToEndEncrypted)
{
    setupPair();
    auto dm = alice->call(QStringLiteral("dm.open"), {{"user", "bob"}});
    ASSERT_TRUE(dm.ok);
    const QString dmId = dm.result.value("id").toString();

    // Text: both daemons read it, the server only ever sees ciphertext.
    auto sent = alice->call(QStringLiteral("message.send"), {{"channel", dmId}, {"content", "top secret plan"}});
    ASSERT_TRUE(sent.ok) << sent.errorMessage.toStdString();
    EXPECT_EQ(sent.result.value("content").toString(), QStringLiteral("top secret plan"));
    EXPECT_EQ(sent.result.value("e2e").toString(), QStringLiteral("ok"));
    auto got = bob->waitEvent(QStringLiteral("message.created"),
        [](const QJsonObject& m) { return m.value("content").toString() == u"top secret plan"; });
    ASSERT_TRUE(got);
    EXPECT_EQ(got->value("e2e").toString(), QStringLiteral("ok"));

    RawClient raw(server);
    ASSERT_TRUE(raw.connect() && raw.hello() && raw.login("bob", "bob-password"));
    proto::Envelope hist;
    hist.mutable_get_messages()->set_channel_id(dmId.toULongLong());
    auto page = raw.call(hist);
    ASSERT_TRUE(page && page->has_message_page() && page->message_page().messages_size() == 1);
    const auto& stored = page->message_page().messages(0);
    EXPECT_TRUE(stored.content().empty());
    EXPECT_FALSE(stored.encrypted().empty());
    EXPECT_EQ(stored.encrypted().find("top secret"), std::string::npos);

    // Server channels stay readable by the server.
    ASSERT_TRUE(alice->call(QStringLiteral("message.send"), {{"channel", "general"}, {"content", "in the open"}}).ok);
    auto open = bob->waitEvent(QStringLiteral("message.created"),
        [](const QJsonObject& m) { return m.value("content").toString() == u"in the open"; });
    ASSERT_TRUE(open);
    EXPECT_FALSE(open->contains("e2e"));

    // Edits stay encrypted.
    ASSERT_TRUE(alice
            ->call(QStringLiteral("message.edit"),
                {{"message", sent.result.value("id")}, {"content", "top secret plan, revised"}})
            .ok);
    EXPECT_TRUE(bob->waitEvent(QStringLiteral("message.updated"),
        [](const QJsonObject& m) { return m.value("content").toString() == u"top secret plan, revised"; }));

    // Files: the server stores ciphertext under a meaningless name.
    QTemporaryDir files;
    const QString source = files.filePath(QStringLiteral("passwords.txt"));
    const QByteArray secret = QByteArray("hunter2 ").repeated(20000);
    {
        QFile f(source);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(secret);
    }
    auto withFile = alice->call(QStringLiteral("message.send"),
        {{"channel", dmId}, {"content", "see attached"}, {"files", QJsonArray{source}}});
    ASSERT_TRUE(withFile.ok) << withFile.errorMessage.toStdString();
    auto fileEv = bob->waitEvent(QStringLiteral("message.created"),
        [](const QJsonObject& m) { return m.value("content").toString() == u"see attached"; });
    ASSERT_TRUE(fileEv);
    const QJsonObject att = fileEv->value("attachments").toArray().at(0).toObject();
    EXPECT_EQ(att.value("filename").toString(), QStringLiteral("passwords.txt"));
    EXPECT_EQ(att.value("size").toDouble(), static_cast<double>(secret.size()));
    EXPECT_TRUE(att.value("encrypted").toBool());

    proto::Envelope raw2;
    raw2.mutable_get_messages()->set_channel_id(dmId.toULongLong());
    auto page2 = raw.call(raw2);
    ASSERT_TRUE(page2 && page2->has_message_page());
    const auto& storedFile = page2->message_page().messages(0).attachments(0);
    EXPECT_EQ(storedFile.filename().find("passwords"), std::string::npos);
    proto::Envelope dl;
    dl.mutable_download()->set_attachment_id(storedFile.id());
    auto chunk = raw.call(dl);
    ASSERT_TRUE(chunk && chunk->has_file_chunk());
    EXPECT_EQ(chunk->file_chunk().data().find("hunter2"), std::string::npos);

    QTemporaryDir out;
    auto saved = bob->call(QStringLiteral("attachment.download"),
        {{"attachment", att.value("id")}, {"filename", att.value("filename")}, {"to", out.path()}});
    ASSERT_TRUE(saved.ok) << saved.errorMessage.toStdString();
    QFile back(saved.result.value("path").toString());
    ASSERT_TRUE(back.open(QIODevice::ReadOnly));
    EXPECT_TRUE(back.readAll() == secret);

    // Safety numbers agree; a new device of Bob's changes them and warns Alice.
    const auto aliceView = alice->call(QStringLiteral("e2e.safety"), {{"user", "bob"}}).result;
    const auto bobView = bob->call(QStringLiteral("e2e.safety"), {{"user", "alice"}}).result;
    EXPECT_EQ(aliceView.value("number").toString(), bobView.value("number").toString());
    ASSERT_TRUE(alice->call(QStringLiteral("e2e.verify"), {{"user", "bob"}}).result.value("verified").toBool());

    TestDaemon bobLaptop(QStringLiteral("bob-laptop"));
    ASSERT_TRUE(bobLaptop.start());
    ASSERT_TRUE(bobLaptop.loginOn(server, QStringLiteral("bob"), QStringLiteral("bob-password")));
    auto changed = alice->waitEvent(QStringLiteral("e2e.keys_changed"), {}, 10000);
    ASSERT_TRUE(changed);
    const auto after = alice->call(QStringLiteral("e2e.safety"), {{"user", "bob"}}).result;
    EXPECT_NE(after.value("number").toString(), aliceView.value("number").toString());
    EXPECT_FALSE(after.value("verified").toBool()) << "a new device voids the verification";
    EXPECT_EQ(after.value("devices").toInt(), 2);
    // New messages reach both of Bob's devices.
    ASSERT_TRUE(alice->call(QStringLiteral("message.send"), {{"channel", dmId}, {"content", "both of you"}}).ok);
    EXPECT_TRUE(bobLaptop.waitEvent(QStringLiteral("message.created"),
        [](const QJsonObject& m) { return m.value("content").toString() == u"both of you"; }));
}

TEST_F(DaemonFixture, NoPlaintextFallbackWhenSomeoneHasNoKeys)
{
    setupPair();
    // Mallory uses an old client: she never publishes a device key.
    RawClient mallory(server);
    ASSERT_TRUE(mallory.connect() && mallory.hello() && mallory.registerUser("mallory", "mallory-password"));
    auto inv = alice->call(QStringLiteral("invite.create"), {{"server", "Sleepy Studio"}});
    proto::Envelope join;
    join.mutable_join_invite()->set_token(inv.result.value("token").toString().toStdString());
    ASSERT_TRUE(mallory.call(join)->has_server());
    ASSERT_TRUE(waitFor([&] { return alice->daemon().connection().model().resolveUser("mallory") != 0; }));

    auto sent = alice->call(QStringLiteral("dm.send"), {{"user", "mallory"}, {"content", "hi"}});
    EXPECT_FALSE(sent.ok);
    EXPECT_TRUE(sent.errorMessage.contains(QStringLiteral("encryption"))) << sent.errorMessage.toStdString();
}
