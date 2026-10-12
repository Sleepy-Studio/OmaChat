#include "RecoveryFixture.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

using namespace omachat;
using namespace omachat::test;

namespace {
struct SendRecoveryDaemon : ::testing::Test {
    QTemporaryDir directory;
    TestServer server;
    std::unique_ptr<RecoveryDaemon> client;
    QString channel;
    void SetUp() override
    {
        ASSERT_TRUE(directory.isValid());
        ASSERT_TRUE(server.start());
        client = std::make_unique<RecoveryDaemon>(directory.path());
        ASSERT_TRUE(client->start());
        ASSERT_TRUE(client->authenticate(server, "recovery-user"));
        const auto community = client->call("server.create", {{"name", "Recovery Studio"}});
        ASSERT_TRUE(community.ok) << community.errorMessage.toStdString();
        const auto channels = client->daemon->connection().model().snapshotJson().value("channels").toArray();
        for (const auto& c : channels)
            if (c.toObject().value("name").toString() == "general")
                channel = c.toObject().value("id").toString();
        ASSERT_FALSE(channel.isEmpty());
    }
    QJsonObject send(QString id = "operation", QString text = "recovery fixture message") const
    {
        return {{"channel", channel}, {"content", text}, {"original", text}, {"recovery_id", id}};
    }
    QJsonArray records()
    {
        const auto reply = client->call("recovery.list");
        EXPECT_TRUE(reply.ok) << reply.errorMessage.toStdString();
        return reply.result.value("records").toArray();
    }
    bool seed(QString id, daemon::SendRecoveryStore::State state)
    {
        RecoveryKeyring keys(client->keyring);
        daemon::SendRecoveryStore storage(directory.filePath("send-recovery"), keys);
        daemon::SendRecoveryStore::Record record;
        record.id = id;
        record.channelId = channel;
        record.content = "seeded private body";
        record.original = "newer composer must survive";
        record.createdAt = QDateTime::currentDateTimeUtc();
        bool result = false;
        storage.stage(client->binding(), record, [&](bool ok, const QString&) { result = ok; });
        if (result && state == daemon::SendRecoveryStore::State::Rejected)
            storage.reject(client->binding(), id, [&](bool ok, const QString&) { result = ok; });
        return result;
    }
};
} // namespace

TEST_F(SendRecoveryDaemon, DurableAcknowledgementDeletesAndEmitsChange)
{
    int events = 0;
    ASSERT_TRUE(client->call("events.subscribe").ok);
    QObject::connect(
        &client->ipc, &ipc::IpcClient::eventReceived, &client->ipc, [&](const QString& name, const QJsonObject&) {
            if (name == "recovery.changed")
                ++events;
        });
    EXPECT_TRUE(client->daemon->statusJson().value("send_recovery").toObject().value("supported").toBool());
    const auto reply = client->call("message.send", send());
    ASSERT_TRUE(reply.ok) << reply.errorMessage.toStdString();
    EXPECT_EQ(reply.result.value("content").toString(), "recovery fixture message");
    EXPECT_TRUE(records().isEmpty());
    EXPECT_GE(events, 2);
}

TEST_F(SendRecoveryDaemon, ExplicitServerRejectionRemainsRetryableAcrossRestart)
{
    const auto params = send("rejected", ""); // Server rejects an empty message.
    const auto rejected = client->call("message.send", params);
    ASSERT_FALSE(rejected.ok);
    ASSERT_EQ(rejected.errorCode, "BadRequest");
    auto before = records();
    ASSERT_EQ(before.size(), 1);
    EXPECT_EQ(before[0].toObject().value("state").toString(), "rejected");
    client->stop();
    ASSERT_TRUE(client->start());
    ASSERT_TRUE(client->connected());
    auto restored = records();
    ASSERT_EQ(restored.size(), 1);
    EXPECT_EQ(restored[0].toObject().value("state").toString(), "rejected");
    EXPECT_EQ(client->call("message.send", params).errorCode, "BadRequest");
    EXPECT_EQ(records()[0].toObject().value("state").toString(), "rejected");
    ASSERT_TRUE(client->call("recovery.dismiss", {{"id", "rejected"}}).ok);
    EXPECT_TRUE(records().isEmpty());
}

TEST_F(SendRecoveryDaemon, RestartPendingBecomesUncertainAndCannotResend)
{
    ASSERT_TRUE(seed("crash-window", daemon::SendRecoveryStore::State::Pending));
    client->stop();
    ASSERT_TRUE(client->start());
    ASSERT_TRUE(client->connected());
    const auto restored = records();
    ASSERT_EQ(restored.size(), 1);
    EXPECT_EQ(restored[0].toObject().value("state").toString(), "uncertain");
    EXPECT_EQ(restored[0].toObject().value("original").toString(), "newer composer must survive");
    EXPECT_EQ(client->call("message.send", send("crash-window")).errorCode, "RecoveryError");
    auto history = client->call("message.history", {{"channel", channel}});
    ASSERT_TRUE(history.ok);
    EXPECT_TRUE(history.result.value("messages").toArray().isEmpty());
}

TEST_F(SendRecoveryDaemon, KeyWriteFailureDoesNotDispatch)
{
    client->keyring->failRecoveryWrite = true;
    const auto failed = client->call("message.send", send());
    EXPECT_FALSE(failed.ok);
    EXPECT_EQ(failed.errorCode, "RecoveryError");
    const auto history = client->call("message.history", {{"channel", channel}});
    ASSERT_TRUE(history.ok);
    EXPECT_TRUE(history.result.value("messages").toArray().isEmpty());
}

TEST_F(SendRecoveryDaemon, AcknowledgementCleanupFailureIsUncertainNeverRetryable)
{
    client->keyring->failRecoveryReadOn = client->keyring->recoveryReadCount + 2;
    const auto reply = client->call("message.send", send("ack-crash"));
    EXPECT_FALSE(reply.ok);
    EXPECT_EQ(reply.errorCode, "RecoveryError");
    auto history = client->call("message.history", {{"channel", channel}});
    ASSERT_TRUE(history.ok);
    ASSERT_EQ(history.result.value("messages").toArray().size(), 1);
    client->stop();
    ASSERT_TRUE(client->start());
    ASSERT_TRUE(client->connected());
    auto restored = records();
    ASSERT_EQ(restored.size(), 1);
    EXPECT_EQ(restored[0].toObject().value("state").toString(), "uncertain");
    EXPECT_EQ(client->call("message.send", send("ack-crash")).errorCode, "RecoveryError");
}

TEST_F(SendRecoveryDaemon, DismissAndLogoutFailuresAreVisibleAndKeepAccount)
{
    ASSERT_TRUE(seed("unresolved", daemon::SendRecoveryStore::State::Rejected));
    client->keyring->failRecoveryRead = true;
    auto dismissed = client->call("recovery.dismiss", {{"id", "unresolved"}});
    EXPECT_FALSE(dismissed.ok);
    EXPECT_EQ(dismissed.errorCode, "RecoveryError");
    client->keyring->failRecoveryRead = false;
    ASSERT_EQ(records().size(), 1);
    client->keyring->failRecoveryRemove = true;
    auto logout = client->call("account.logout");
    EXPECT_FALSE(logout.ok);
    EXPECT_EQ(client->daemon->statusJson().value("state").toString(), "connected");
    client->keyring->failRecoveryRemove = false;
    ASSERT_TRUE(client->call("account.logout").ok);
    EXPECT_FALSE(QFileInfo(directory.filePath("send-recovery")).exists()
        && !QDir(directory.filePath("send-recovery")).entryList(QDir::Files).isEmpty());
}

TEST_F(SendRecoveryDaemon, AccountSwitchHidesUnrelatedRecords)
{
    ASSERT_TRUE(seed("alice-send", daemon::SendRecoveryStore::State::Rejected));
    const auto first = client->binding().accountId;
    ASSERT_TRUE(client->authenticate(server, "recovery-other-user"));
    EXPECT_NE(client->binding().accountId, first);
    EXPECT_TRUE(records().isEmpty());
    ASSERT_TRUE(client->call("account.switch", {{"account", first}}).ok);
    ASSERT_TRUE(client->connected());
    EXPECT_EQ(records().size(), 1);
}

TEST_F(SendRecoveryDaemon, AuthenticatedTlsPeerBindsWithoutSavedManualPinAndRejectsMismatch)
{
    auto account = client->daemon->connection().account();
    const auto actualPeer = client->daemon->connection().verifiedPeerFingerprint();
    ASSERT_FALSE(actualPeer.isEmpty());
    // Exercises binding with an absent manual pin on an already authenticated
    // TLS transport. The fixture's self-signed handshake itself used explicit
    // trust; this is not a CA-chain validation test.
    account.trustedFingerprint.clear();
    client->daemon->connection().updateAccount(account);
    auto capability = client->daemon->statusJson().value("send_recovery").toObject();
    EXPECT_TRUE(capability.value("supported").toBool());
    EXPECT_EQ(capability.value("certificate_fingerprint").toString(), actualPeer);
    ASSERT_TRUE(client->call("message.send", send("system-trusted")).ok);
    account.trustedFingerprint = "mismatching-explicit-pin";
    client->daemon->connection().updateAccount(account);
    capability = client->daemon->statusJson().value("send_recovery").toObject();
    EXPECT_FALSE(capability.value("supported").toBool());
    EXPECT_TRUE(capability.value("certificate_fingerprint").toString().isEmpty());
    EXPECT_FALSE(client->call("message.send", send("wrong-pin")).ok);
}

TEST_F(SendRecoveryDaemon, DurableUploadResumesForTheSameAuthenticatedAccount)
{
    const auto source = directory.filePath("resumable.bin");
    {
        QFile file(source);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write(QByteArray(24 * 1024 * 1024, 'r')), 24 * 1024 * 1024);
    }
    ASSERT_TRUE(client->call("events.subscribe").ok);
    bool begun = false;
    QObject observer;
    QObject::connect(
        &client->ipc, &ipc::IpcClient::eventReceived, &observer, [&](const QString& name, const QJsonObject& data) {
            if (name == "transfer.progress" && data.value("attachment_id").toString() != "0"
                && !data.contains("complete"))
                begun = true;
        });
    auto result = std::make_shared<std::optional<ipc::Reply>>();
    auto params = send("resumable");
    params.insert("files", QJsonArray{source});
    const auto identity = client->daemon->connection().identityGeneration();
    const auto socketGeneration = client->daemon->connection().linkGeneration();
    client->ipc.request("message.send", params, [result](const ipc::Reply& reply) { *result = reply; }, 0);
    ASSERT_TRUE(waitFor([&] { return begun; }, 15000));
    client->daemon->connection().dropLink("recovery fixture ordinary network drop");
    ASSERT_TRUE(waitFor([&] { return result->has_value(); }, 60000));
    ASSERT_TRUE(result->value().ok) << result->value().errorMessage.toStdString();
    EXPECT_EQ(client->daemon->connection().identityGeneration(), identity);
    EXPECT_GT(client->daemon->connection().linkGeneration(), socketGeneration);
    ASSERT_EQ(result->value().result.value("attachments").toArray().size(), 1);
    EXPECT_TRUE(records().isEmpty());
}

TEST_F(SendRecoveryDaemon, AccountSwitchBeforeNextFileDoesNotUploadTheRemainingPath)
{
    const auto originAccount = client->binding().accountId;
    TestServer otherServer;
    ASSERT_TRUE(otherServer.start());
    ASSERT_TRUE(client->authenticate(otherServer, "other-endpoint-user"));
    ASSERT_TRUE(client->call("server.create", {{"name", "Other Recovery Studio"}}).ok);
    const auto targetAccount = client->binding().accountId;
    ASSERT_NE(originAccount, targetAccount);
    ASSERT_TRUE(client->call("account.switch", {{"account", originAccount}}).ok);
    ASSERT_TRUE(client->connected());
    const auto first = directory.filePath("first-origin-file.bin");
    const auto remaining = directory.filePath("remaining-private-path.txt");
    for (const auto& pair : {std::pair{first, 24 * 1024 * 1024}, std::pair{remaining, 1024}}) {
        QFile file(pair.first);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write(QByteArray(pair.second, 'x')), pair.second);
    }
    ASSERT_TRUE(client->call("events.subscribe").ok);
    bool firstBegun = false;
    int remainingEvents = 0;
    QObject observer;
    QObject::connect(
        &client->ipc, &ipc::IpcClient::eventReceived, &observer, [&](const QString& name, const QJsonObject& data) {
            if (name != "transfer.progress")
                return;
            if (data.value("name").toString() == "remaining-private-path.txt")
                ++remainingEvents;
            if (data.value("name").toString() == "first-origin-file.bin"
                && data.value("attachment_id").toString() != "0" && !data.contains("complete"))
                firstBegun = true;
        });
    auto result = std::make_shared<std::optional<ipc::Reply>>();
    auto params = send("switch-files");
    params.insert("files", QJsonArray{first, remaining});
    client->ipc.request("message.send", params, [result](const ipc::Reply& reply) { *result = reply; }, 0);
    ASSERT_TRUE(waitFor([&] { return firstBegun; }, 15000));
    // Pause the already authorized first transfer to make switching occur
    // deterministically before its callback can consider the second path.
    client->daemon->connection().dropLink("recovery fixture pause before account switch");
    ASSERT_TRUE(client->call("account.switch", {{"account", targetAccount}}).ok);
    ASSERT_TRUE(client->connected());
    ASSERT_TRUE(waitFor([&] { return result->has_value(); }, 60000));
    EXPECT_FALSE(result->value().ok);
    EXPECT_EQ(result->value().errorCode, "RecoveryError");
    EXPECT_EQ(remainingEvents, 0);
    const auto history = client->call("message.history", {{"channel", "general"}});
    ASSERT_TRUE(history.ok);
    EXPECT_TRUE(history.result.value("messages").toArray().isEmpty());
}
