#include "Harness.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>

#include <gtest/gtest.h>

using namespace omachat;
using namespace omachat::test;

namespace {
struct ChannelPreferencesIpc : ::testing::Test {
    TestServer server;
    TestDaemon daemon{"preferences"};
    QString channel;
    QString account;
    std::vector<std::pair<std::int64_t, daemon::Id>> notifications;
    void SetUp() override
    {
        ASSERT_TRUE(server.start());
        ASSERT_TRUE(daemon.start([this](daemon::DaemonOptions& options) {
            options.notifications = true;
            options.notificationSink
                = [this](std::int64_t id, daemon::Id channelId) { notifications.emplace_back(id, channelId); };
        }));
        ASSERT_TRUE(daemon.registerOn(server, "preferences", "test-password"));
        ASSERT_TRUE(daemon.call("server.create", {{"name", "Preferences"}}).ok);
        auto channels = daemon.call("channel.list", {{"server", "Preferences"}});
        ASSERT_TRUE(channels.ok);
        for (const auto& value : channels.result.value("channels").toArray()) {
            if (value.toObject().value("name") == "general")
                channel = value.toObject().value("id").toString();
        }
        ASSERT_FALSE(channel.isEmpty());
        account = daemon.call("daemon.status").result.value("account").toObject().value("id").toString();
    }
    ipc::Reply get() { return daemon.call("channel.preferences.get", {{"channel", channel}}); }
    ipc::Reply set(QJsonObject parameters)
    {
        parameters.insert("channel", channel);
        parameters.insert("account", account);
        return daemon.call("channel.preferences.set", parameters);
    }
    QString databasePath()
    {
        return QFileInfo(daemon.call("config.get").result.value("path").toString()).dir().filePath("local.db");
    }
    void sql(const QString& statement)
    {
        const auto name = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name);
            db.setDatabaseName(databasePath());
            ASSERT_TRUE(db.open());
            QSqlQuery query(db);
            ASSERT_TRUE(query.exec(statement)) << statement.toStdString();
        }
        QSqlDatabase::removeDatabase(name);
    }
};
} // namespace

TEST_F(ChannelPreferencesIpc, TimedMutePreservesLevelAndProjectsAccountTaggedLegacyState)
{
    auto result = get();
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.result.value("notification_level"), "default");
    EXPECT_TRUE(result.result.value("mute_until_ms").isNull());
    const auto before = QDateTime::currentMSecsSinceEpoch();
    auto saved = set({{"notification_level", "all"}, {"mute_for", "1h"}});
    ASSERT_TRUE(saved.ok) << saved.errorMessage.toStdString();
    EXPECT_EQ(saved.result.value("account"), account);
    EXPECT_EQ(saved.result.value("channel_id"), channel);
    const auto deadline = saved.result.value("mute_until_ms").toString().toLongLong();
    EXPECT_GE(deadline, before + 3600000);
    EXPECT_LE(deadline, QDateTime::currentMSecsSinceEpoch() + 3600000);
    EXPECT_TRUE(saved.result.value("muted").toBool());
    auto event = daemon.waitEvent("channel.preferences_changed");
    ASSERT_TRUE(event);
    EXPECT_EQ(*event, saved.result);
    auto legacy = daemon.waitEvent("channel.muted");
    ASSERT_TRUE(legacy);
    EXPECT_EQ(legacy->value("account"), account);
    EXPECT_EQ(legacy->value("channel_id"), channel);
    EXPECT_TRUE(legacy->value("muted").toBool());
    const auto snapshot = daemon.call("state.snapshot");
    ASSERT_TRUE(snapshot.ok);
    EXPECT_EQ(snapshot.result.value("account"), account);
    EXPECT_TRUE(snapshot.result.value("muted_channels").toArray().contains(channel));
    EXPECT_EQ(snapshot.result.value("channel_preferences").toArray().size(), 1);
    auto unmuted = daemon.call("channel.mute", {{"channel", channel}, {"muted", false}});
    ASSERT_TRUE(unmuted.ok);
    EXPECT_EQ(unmuted.result.value("notification_level"), "all");
    EXPECT_TRUE(unmuted.result.value("mute_until_ms").isNull());
    EXPECT_FALSE(unmuted.result.value("muted").toBool());
    ASSERT_TRUE(daemon.call("channel.mute", {{"channel", channel}}).ok);
    EXPECT_EQ(get().result.value("mute_until_ms"), "0");
    EXPECT_EQ(get().result.value("notification_level"), "all");
}

TEST_F(ChannelPreferencesIpc, InvalidParametersAndStorageFailuresDoNotChangeStateOrEmitSuccess)
{
    ASSERT_TRUE(set({{"notification_level", "mentions"}, {"mute_for", "forever"}}).ok);
    ASSERT_TRUE(daemon.waitEvent("channel.preferences_changed"));
    ASSERT_TRUE(daemon.waitEvent("channel.muted"));
    const auto before = get().result;
    for (const auto& invalid : {QJsonObject{}, QJsonObject{{"notification_level", "invalid"}},
             QJsonObject{{"notification_level", 2}}, QJsonObject{{"mute_for", "2h"}}, QJsonObject{{"mute_for", 1}},
             QJsonObject{{"mute_until_ms", "1"}}, QJsonObject{{"notification_level", "all"}, {"unknown", true}}}) {
        const auto reply = set(invalid);
        EXPECT_FALSE(reply.ok);
        EXPECT_EQ(reply.errorCode, "BadRequest");
        EXPECT_EQ(get().result, before);
    }
    auto wrongAccount = daemon.call(
        "channel.preferences.set", {{"account", "999"}, {"channel", channel}, {"notification_level", "all"}});
    EXPECT_EQ(wrongAccount.errorCode, "Conflict");
    EXPECT_EQ(daemon.call("channel.mute", {{"channel", channel}, {"muted", "yes"}}).errorCode, "BadRequest");
    EXPECT_EQ(daemon.call("channel.preferences.get", {{"channel", "9999999"}}).errorCode, "NotFound");
    sql("CREATE TRIGGER fail_preferences BEFORE UPDATE ON channel_preferences BEGIN SELECT RAISE(ABORT,'fixture'); "
        "END");
    EXPECT_EQ(set({{"notification_level", "all"}}).errorCode, "StorageError");
    EXPECT_EQ(daemon.call("channel.mute", {{"channel", channel}, {"muted", false}}).errorCode, "StorageError");
    EXPECT_EQ(get().result, before);
    EXPECT_TRUE(daemon.call("state.snapshot").result.value("muted_channels").toArray().contains(channel));
    EXPECT_FALSE(daemon.waitEvent("channel.preferences_changed", {}, 80));
    EXPECT_FALSE(daemon.waitEvent("channel.muted", {}, 80));
}

TEST_F(ChannelPreferencesIpc, TimerExpiresOverlayAndUpdatesMultipleClientsWithoutReplayingMessages)
{
    ASSERT_TRUE(set({{"notification_level", "all"}, {"mute_for", "forever"}}).ok);
    ASSERT_TRUE(daemon.waitEvent("channel.preferences_changed"));
    ASSERT_TRUE(daemon.waitEvent("channel.muted"));
    ipc::IpcClient second;
    std::vector<QJsonObject> updates;
    QObject::connect(&second, &ipc::IpcClient::eventReceived, [&](const QString& name, const QJsonObject& data) {
        if (name == "channel.preferences_changed")
            updates.push_back(data);
    });
    second.connectToDaemon(QString("/tmp/omachat-test-preferences-%1.sock").arg(QCoreApplication::applicationPid()));
    ASSERT_TRUE(waitFor([&] { return second.isConnected(); }));
    bool subscribed = false;
    second.request("events.subscribe", {}, [&](const ipc::Reply& reply) { subscribed = reply.ok; });
    ASSERT_TRUE(waitFor([&] { return subscribed; }));
    // Short expiry belongs only to this isolated fixture. The production IPC
    // accepts the documented durations, never caller-supplied deadlines.
    const auto deadline = QDateTime::currentMSecsSinceEpoch() + 180;
    sql(QString("UPDATE channel_preferences SET mute_until_ms=%1").arg(deadline));
    ASSERT_TRUE(daemon.call("state.snapshot").ok); // reload + schedule the next expiry
    auto expired = daemon.waitEvent(
        "channel.preferences_changed", [](const QJsonObject& data) { return !data.value("muted").toBool(); });
    ASSERT_TRUE(expired);
    EXPECT_EQ(expired->value("notification_level"), "all");
    EXPECT_EQ(expired->value("mute_until_ms"), QString::number(deadline));
    ASSERT_TRUE(waitFor([&] { return updates.size() == 1; }));
    EXPECT_EQ(updates.front(), *expired);
    EXPECT_TRUE(daemon.call("state.snapshot").result.value("muted_channels").toArray().isEmpty());
    EXPECT_TRUE(daemon.call("message.history", {{"channel", channel}}).result.value("messages").toArray().isEmpty());
    EXPECT_FALSE(daemon.waitEvent("message.created", {}, 80));
}

TEST_F(ChannelPreferencesIpc, AccountSwitchDoesNotInheritPreferencesForTheSameChannel)
{
    ASSERT_TRUE(set({{"notification_level", "none"}, {"mute_for", "forever"}}).ok);
    const auto first = get().result;
    ASSERT_TRUE(daemon.registerOn(server, "other", "other-password"));
    const auto invite = daemon.call("account.switch", {{"account", account}});
    ASSERT_TRUE(invite.ok);
    ASSERT_TRUE(daemon.waitState("connected"));
    const auto invitation = daemon.call("invite.create", {{"server", "Preferences"}});
    ASSERT_TRUE(invitation.ok);
    const auto accounts = daemon.call("account.list").result.value("accounts").toArray();
    QString secondAccount;
    for (const auto& value : accounts) {
        auto entry = value.toObject();
        if (entry.value("id") != account)
            secondAccount = entry.value("id").toString();
    }
    ASSERT_FALSE(secondAccount.isEmpty());
    ASSERT_TRUE(daemon.call("account.switch", {{"account", secondAccount}}).ok);
    ASSERT_TRUE(daemon.waitState("connected"));
    ASSERT_TRUE(daemon.call("server.join", {{"invite", invitation.result.value("uri")}}).ok);
    auto second = get();
    ASSERT_TRUE(second.ok) << second.errorMessage.toStdString();
    EXPECT_EQ(second.result.value("account"), secondAccount);
    EXPECT_EQ(second.result.value("notification_level"), "default");
    EXPECT_FALSE(second.result.value("muted").toBool());
    EXPECT_EQ(set({{"notification_level", "all"}}).errorCode, "Conflict") << "stale GUI account";
    ASSERT_TRUE(daemon.call("account.switch", {{"account", account}}).ok);
    EXPECT_EQ(get().result, first);
    ASSERT_TRUE(daemon.waitEvent("channel.muted")); // initial active-account mute
    sql(QString("UPDATE channel_preferences SET mute_until_ms=%1 WHERE account_id=%2")
            .arg(QDateTime::currentMSecsSinceEpoch() + 180)
            .arg(account));
    ASSERT_TRUE(daemon.call("state.snapshot").ok);
    ASSERT_TRUE(daemon.call("account.switch", {{"account", secondAccount}}).ok);
    ASSERT_TRUE(daemon.waitEvent("channel.preferences_changed",
        [&](const QJsonObject& value) { return value.value("account") == account && !value.value("muted").toBool(); }));
    EXPECT_FALSE(daemon.waitEvent(
        "channel.muted",
        [&](const QJsonObject& value) { return value.value("account") == account && !value.value("muted").toBool(); },
        80))
        << "old GUIs must not receive a background channel's legacy projection";
    EXPECT_EQ(get().result.value("notification_level"), "default");
}

TEST_F(ChannelPreferencesIpc, ActiveAndBackgroundDeliveryUsesOriginatingPreferencesAndGlobalOptOuts)
{
    TestDaemon sender("preference-sender");
    ASSERT_TRUE(sender.start());
    ASSERT_TRUE(sender.registerOn(server, "sender", "test-password"));
    const auto invite = daemon.call("invite.create", {{"server", "Preferences"}});
    ASSERT_TRUE(invite.ok);
    ASSERT_TRUE(sender.call("server.join", {{"invite", invite.result.value("uri")}}).ok);
    auto send = [&](const QString& target, const QString& text) {
        const auto sent = sender.call("message.send", {{"channel", target}, {"content", text}});
        EXPECT_TRUE(sent.ok) << sent.errorMessage.toStdString();
        const auto id = sent.result.value("id").toString();
        EXPECT_TRUE(
            daemon.waitEvent("message.created", [&](const QJsonObject& data) { return data.value("id") == id; }));
    };
    send(channel, "ordinary message");
    EXPECT_TRUE(notifications.empty());
    ASSERT_TRUE(set({{"notification_level", "all"}}).ok);
    send(channel, "all messages enabled");
    ASSERT_EQ(notifications.size(), 1u);
    EXPECT_EQ(notifications.back().first, account.toLongLong());
    EXPECT_EQ(notifications.back().second, channel.toULongLong());
    ASSERT_TRUE(set({{"mute_for", "1h"}}).ok);
    send(channel, "silenced by overlay");
    EXPECT_EQ(notifications.size(), 1u);
    // Expired on reload: no delivery replay, but the next newly arriving message is eligible.
    sql("UPDATE channel_preferences SET mute_until_ms=1");
    ASSERT_TRUE(daemon.call("state.snapshot").ok);
    EXPECT_EQ(notifications.size(), 1u);
    send(channel, "eligible after expiry");
    EXPECT_EQ(notifications.size(), 2u);
    ASSERT_TRUE(set({{"notification_level", "mentions"}}).ok);
    send(channel, "ordinary message again");
    EXPECT_EQ(notifications.size(), 2u);
    send(channel, "hello @preferences");
    EXPECT_EQ(notifications.size(), 3u);
    ASSERT_TRUE(daemon.call("config.set_notifications", {{"mentions", false}}).ok);
    ASSERT_TRUE(set({{"notification_level", "all"}}).ok);
    send(channel, "global community switch is off @preferences");
    EXPECT_EQ(notifications.size(), 3u);
    ASSERT_TRUE(daemon.call("config.set_notifications", {{"mentions", true}}).ok);
    ASSERT_TRUE(daemon.call("ui.focus", {{"channel", channel}, {"focused", true}}).ok);
    send(channel, "focused conversation");
    EXPECT_EQ(notifications.size(), 3u);
    ASSERT_TRUE(daemon.call("ui.focus", {{"focused", false}}).ok);
    ASSERT_TRUE(daemon.call("presence.set", {{"status", "dnd"}}).ok);
    send(channel, "do not disturb");
    EXPECT_EQ(notifications.size(), 3u);
    ASSERT_TRUE(daemon.call("presence.set", {{"status", "online"}}).ok);
    // Keep the first account connected in the background; it must retain All.
    ASSERT_TRUE(daemon.registerOn(server, "background-active", "test-password"));
    auto sent = sender.call("message.send", {{"channel", channel}, {"content", "background delivery"}});
    ASSERT_TRUE(sent.ok);
    ASSERT_TRUE(waitFor([&] { return notifications.size() == 4; }));
    EXPECT_EQ(notifications.back().first, account.toLongLong());
    ASSERT_TRUE(daemon.call("account.switch", {{"account", account}}).ok);
    // Encrypted individual mentions are decoded by ClientState before policy evaluation.
    const auto dm = sender.call("dm.open", {{"user", "preferences"}});
    ASSERT_TRUE(dm.ok);
    const auto dmId = dm.result.value("id").toString();
    ASSERT_TRUE(waitFor([&] { return daemon.daemon().connection().model().channel(dmId.toULongLong()) != nullptr; }));
    ASSERT_TRUE(daemon.call("channel.preferences.set", {{"channel", dmId}, {"notification_level", "mentions"}}).ok);
    send(dmId, "ordinary encrypted direct message");
    EXPECT_EQ(notifications.size(), 4u);
    send(dmId, "encrypted hello @preferences");
    EXPECT_EQ(notifications.size(), 5u);
    ASSERT_TRUE(daemon.call("config.set_notifications", {{"messages", false}}).ok);
    send(dmId, "encrypted @preferences with DM master disabled");
    EXPECT_EQ(notifications.size(), 5u);
}

TEST_F(ChannelPreferencesIpc, CliKeepsLegacyCommandsAndAddsValidatedDurationsAndLevels)
{
    const auto binary = QCoreApplication::applicationDirPath() + "/../cli/omachatctl";
    ASSERT_TRUE(QFileInfo::exists(binary));
    auto run = [&](const QStringList& arguments, bool success) {
        QProcess process;
        QStringList argv{"--json", "--socket",
            QString("/tmp/omachat-test-preferences-%1.sock").arg(QCoreApplication::applicationPid())};
        argv.append(arguments);
        process.start(binary, argv);
        EXPECT_TRUE(waitFor([&] { return process.state() == QProcess::NotRunning; }));
        EXPECT_EQ(process.exitStatus(), QProcess::NormalExit);
        EXPECT_EQ(process.exitCode() == 0, success) << process.readAllStandardError().toStdString();
        return QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    };
    auto level = run({"channel", "notifications", channel, "all"}, true);
    EXPECT_EQ(level.value("notification_level"), "all");
    for (const auto& duration : {"1h", "8h", "24h", "forever"}) {
        auto muted = run({"channel", "mute", channel, "--for", duration}, true);
        EXPECT_TRUE(muted.value("muted").toBool());
        EXPECT_EQ(muted.value("notification_level"), "all");
        EXPECT_EQ(muted.value("account"), account);
    }
    EXPECT_TRUE(run({"channel", "unmute", channel}, true).value("mute_until_ms").isNull());
    EXPECT_EQ(run({"channel", "mute", channel}, true).value("mute_until_ms"), "0");
    run({"channel", "mute", channel, "--for", "2h"}, false);
    run({"channel", "mute", channel, "--for"}, false);
    run({"channel", "unmute", channel, "--for", "1h"}, false);
    run({"channel", "notifications", channel, "invalid"}, false);
    EXPECT_EQ(run({"channel", "favorite", channel}, true).value("account"), account);
    EXPECT_EQ(run({"channel", "hide", channel}, true).value("scope"), "device");
    EXPECT_EQ(run({"sidebar", "get"}, true).value("channels").toArray().size(), 1);
    EXPECT_TRUE(run({"sidebar", "compact", "on"}, true).value("compact").toBool());
    EXPECT_FALSE(run({"sidebar", "participants", "off"}, true).value("show_voice_participants").toBool());
    run({"channel", "unhide", channel}, true);
    run({"channel", "favorite", channel, "up"}, true);
    run({"channel", "favorite", channel, "remove"}, true);
    run({"channel", "favorite", channel, "sideways"}, false);
    run({"sidebar", "compact", "invalid"}, false);
}

TEST_F(ChannelPreferencesIpc, SidebarOrderingPersistenceAndCheckedFailures)
{
    auto sidebar = daemon.call("sidebar.get");
    ASSERT_TRUE(sidebar.ok);
    EXPECT_EQ(sidebar.result.value("scope"), "device");
    const auto other = daemon.call("channel.create", {{"server", "Preferences"}, {"name", "second"}, {"type", "text"}});
    ASSERT_TRUE(other.ok) << other.errorMessage.toStdString();
    const auto second = other.result.value("id").toString();
    ASSERT_FALSE(second.isEmpty());
    auto save = [&](const QString& id, QJsonObject p) {
        p.insert("account", account);
        p.insert("channel", id);
        return daemon.call("sidebar.set", p);
    };
    ASSERT_TRUE(save(channel, {{"favorite", true}, {"hidden", true}}).ok);
    ASSERT_TRUE(save(second, {{"favorite", true}}).ok);
    ASSERT_TRUE(save(channel, {{"hidden", false}, {"move", "down"}}).ok);
    auto channels = daemon.call("sidebar.get").result.value("channels").toArray();
    QHash<QString, QJsonObject> byId;
    for (const auto& value : channels)
        byId.insert(value.toObject().value("channel_id").toString(), value.toObject());
    EXPECT_EQ(byId.value(second).value("favorite_rank"), "0");
    EXPECT_EQ(byId.value(channel).value("favorite_rank"), "1");
    EXPECT_FALSE(byId.value(channel).value("hidden").toBool());
    ASSERT_TRUE(daemon.call("sidebar.set", {{"compact", true}, {"show_voice_participants", false}}).ok);
    const auto before = daemon.call("sidebar.get").result;
    EXPECT_EQ(daemon.call("state.snapshot").result.value("sidebar_preferences"), before);
    EXPECT_EQ(save(channel, {{"hidden", "true"}}).errorCode, "BadRequest");
    EXPECT_EQ(save(channel, {{"move", "sideways"}}).errorCode, "BadRequest");
    EXPECT_EQ(save(channel, {{"collapsed", true}}).errorCode, "BadRequest");
    EXPECT_EQ(daemon.call("sidebar.set", {{"compact", true}, {"account", "999"}}).errorCode, "Conflict");
    EXPECT_EQ(daemon.call("sidebar.set", {{"unknown", true}}).errorCode, "BadRequest");
    EXPECT_EQ(daemon.call("sidebar.get").result, before);
    sql("CREATE TRIGGER fail_sidebar BEFORE UPDATE ON sidebar_views BEGIN SELECT RAISE(ABORT,'fixture'); END");
    EXPECT_EQ(save(channel, {{"hidden", true}}).errorCode, "StorageError");
    EXPECT_EQ(daemon.call("sidebar.get").result, before);
    sql("DROP TRIGGER fail_sidebar");
    ASSERT_TRUE(daemon.call("channel.delete", {{"channel", second}}).ok);
    ASSERT_TRUE(waitFor([&] {
        for (const auto& value : daemon.call("sidebar.get").result.value("channels").toArray())
            if (value.toObject().value("channel_id") == second)
                return false;
        return true;
    }));
    // Hiding/favoriting must never alter the notification setting.
    EXPECT_EQ(get().result.value("notification_level"), "default");
    EXPECT_FALSE(get().result.value("muted").toBool());
}

TEST_F(ChannelPreferencesIpc, SidebarMovesPastHiddenSelectedAndActiveVoiceFavorites)
{
    const auto created
        = daemon.call("channel.create", {{"server", "Preferences"}, {"name", "second"}, {"type", "text"}});
    ASSERT_TRUE(created.ok);
    const auto second = created.result.value("id").toString();
    auto save = [&](const QString& id, QJsonObject changes) {
        changes.insert("account", account);
        changes.insert("channel", id);
        return daemon.call("sidebar.set", changes);
    };
    auto rank = [&](const QString& id) {
        for (const auto& value : daemon.call("sidebar.get").result.value("channels").toArray()) {
            const auto item = value.toObject();
            if (item.value("channel_id") == id)
                return item.value("favorite_rank").toString();
        }
        return QString();
    };
    ASSERT_TRUE(save(channel, {{"favorite", true}, {"hidden", true}}).ok);
    ASSERT_TRUE(save(second, {{"favorite", true}}).ok);
    ASSERT_TRUE(save(second, {{"move", "up"}, {"selected_channel", channel}}).ok);
    EXPECT_EQ(rank(second), "0");
    EXPECT_EQ(rank(channel), "1");
    ASSERT_TRUE(save(second, {{"move", "down"}, {"selected_channel", channel}}).ok);
    EXPECT_EQ(rank(channel), "0");
    EXPECT_EQ(rank(second), "1");

    const auto voiceCreated
        = daemon.call("channel.create", {{"server", "Preferences"}, {"name", "favorite-call"}, {"type", "voice"}});
    ASSERT_TRUE(voiceCreated.ok);
    const auto voice = voiceCreated.result.value("id").toString();
    ASSERT_TRUE(save(voice, {{"favorite", true}, {"hidden", true}}).ok);
    ASSERT_TRUE(save(second, {{"move", "down"}, {"selected_channel", channel}}).ok);
    EXPECT_EQ(rank(second), "1") << "inactive hidden favorite is excluded";
    ASSERT_TRUE(daemon.call("voice.join", {{"channel", voice}}).ok);
    ASSERT_TRUE(save(second, {{"move", "down"}, {"selected_channel", channel}}).ok);
    EXPECT_EQ(rank(voice), "1");
    EXPECT_EQ(rank(second), "2");
    ASSERT_TRUE(daemon.call("voice.leave").ok);

    ASSERT_TRUE(daemon.call("server.create", {{"name", "Other section"}}).ok);
    const auto otherCreated
        = daemon.call("channel.create", {{"server", "Other section"}, {"name", "other"}, {"type", "text"}});
    ASSERT_TRUE(otherCreated.ok);
    const auto before = daemon.call("sidebar.get").result;
    for (const auto& hint : {QJsonValue(42), QJsonValue(""), QJsonValue("general"), QJsonValue("999999999"),
             QJsonValue(otherCreated.result.value("id"))}) {
        EXPECT_EQ(save(second, {{"move", "up"}, {"selected_channel", hint}}).errorCode, "BadRequest");
        EXPECT_EQ(daemon.call("sidebar.get").result, before);
    }
    EXPECT_EQ(save(second, {{"hidden", false}, {"selected_channel", channel}}).errorCode, "BadRequest");
    EXPECT_EQ(daemon.call("sidebar.get").result, before);
}
