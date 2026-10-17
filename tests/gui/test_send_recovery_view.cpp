#include "RecoveryFixture.hpp"
#include "controllers/AppController.hpp"
#include "platform/ThemeProvider.hpp"
#include "storage/SendRecoveryStore.hpp"

#include <QFile>
#include <QGuiApplication>
#include <QTest>
#include <gtest/gtest.h>

using namespace omachat;
using namespace omachat::test;

namespace {
class SendRecoveryView : public ::testing::Test {
protected:
    TestServer server;
    QTemporaryDir directory;
    RecoveryDaemon daemon{directory.path()};
    client::ThemeProvider theme{nullptr};
    std::unique_ptr<client::AppController> app;
    QString channelId;
    omachat::daemon::SendRecoveryStore::Binding originalBinding;
    int composerRestores = 0;

    void SetUp() override
    {
        ASSERT_TRUE(server.start());
        ASSERT_TRUE(daemon.start());
        ASSERT_TRUE(daemon.authenticate(server, "recovery-view"));
        ASSERT_TRUE(daemon.call("server.create", {{"name", "Recovery view"}}).ok);
        const auto channels = daemon.call("channel.list");
        ASSERT_TRUE(channels.ok);
        for (const auto& value : channels.result.value("channels").toArray()) {
            const auto channel = value.toObject();
            if (channel.value("name").toString() == "general")
                channelId = channel.value("id").toString();
        }
        ASSERT_FALSE(channelId.isEmpty());
        originalBinding = daemon.binding();
        qputenv("OMACHAT_SOCKET", daemon.socketPath.toUtf8());
        qputenv("XDG_CACHE_HOME", directory.filePath("cache").toUtf8());
    }
    void TearDown() override
    {
        app.reset();
        qunsetenv("OMACHAT_SOCKET");
        qunsetenv("XDG_CACHE_HOME");
    }
    void seed(bool rejected = false)
    {
        RecoveryKeyring keys(daemon.keyring);
        omachat::daemon::SendRecoveryStore store(directory.filePath("send-recovery"), keys);
        omachat::daemon::SendRecoveryStore::Record record;
        record.id = "restored-operation";
        record.channelId = channelId;
        record.content = "saved body";
        record.original = "saved composer draft";
        record.createdAt = QDateTime::currentDateTimeUtc();
        bool success = false;
        store.stage(originalBinding, record, [&](bool ok, const QString&) { success = ok; });
        ASSERT_TRUE(success);
        if (rejected) {
            store.reject(originalBinding, record.id, [&](bool ok, const QString&) { success = ok; });
            ASSERT_TRUE(success);
        }
    }
    void startClient()
    {
        config::ClientConfig config;
        config.startup.launchDaemon = false;
        app = std::make_unique<client::AppController>(config);
        QObject::connect(
            app.get(), &client::AppController::composerRestore, app.get(), [&](const QString&) { ++composerRestores; });
        app->start();
        ASSERT_TRUE(waitFor([&] { return app->canSend(); }));
    }
    QVariantMap operation()
    {
        if (!waitFor([&] { return !app->sendOperations().isEmpty(); }))
            return {};
        return app->sendOperations().first().toMap();
    }
    int savedRecords()
    {
        const auto reply = daemon.call("recovery.list");
        if (!reply.ok)
            return -1;
        return reply.result.value("records").toArray().size();
    }
};

TEST_F(SendRecoveryView, DaemonRestartRestoresUncertainSendWithoutRetryOrComposerOverwrite)
{
    seed();
    daemon.stop();
    ASSERT_TRUE(daemon.start());
    ASSERT_TRUE(daemon.connected());
    startClient();
    const auto restored = operation();
    ASSERT_FALSE(restored.isEmpty());
    EXPECT_FALSE(restored.value("pending").toBool());
    EXPECT_FALSE(restored.value("retryable").toBool());
    EXPECT_EQ(restored.value("text").toString(), "saved composer draft");
    EXPECT_TRUE(restored.value("error").toString().contains("uncertain"));
    QFile newerFile(directory.filePath("newer-composer.txt"));
    ASSERT_TRUE(newerFile.open(QIODevice::WriteOnly));
    newerFile.write("newer file");
    newerFile.close();
    app->addFiles({QUrl::fromLocalFile(newerFile.fileName())});
    const auto newerFiles = app->pendingFiles();
    app->retrySend("restored-operation");
    QTest::qWait(100);
    EXPECT_EQ(app->pendingFiles(), newerFiles);
    EXPECT_EQ(composerRestores, 0);
    EXPECT_EQ(savedRecords(), 1);
    const auto history = daemon.call("message.history", {{"channel", channelId}});
    ASSERT_TRUE(history.ok);
    EXPECT_TRUE(history.result.value("messages").toArray().isEmpty());
}

TEST_F(SendRecoveryView, ExplicitRejectedRetrySendsOnceAndAcknowledgementDeletesRecovery)
{
    seed(true);
    startClient();
    const auto restored = operation();
    ASSERT_FALSE(restored.isEmpty());
    EXPECT_TRUE(restored.value("retryable").toBool());
    EXPECT_EQ(savedRecords(), 1);
    app->retrySend("restored-operation");
    app->retrySend("restored-operation");
    ASSERT_TRUE(waitFor([&] { return app->sendOperations().isEmpty(); }))
        << (app->sendOperations().isEmpty() ? QString()
                                            : app->sendOperations().first().toMap().value("error").toString())
               .toStdString();
    EXPECT_EQ(savedRecords(), 0);
    EXPECT_EQ(composerRestores, 0);
    const auto history = daemon.call("message.history", {{"channel", channelId}});
    ASSERT_TRUE(history.ok);
    ASSERT_EQ(history.result.value("messages").toArray().size(), 1);
    EXPECT_EQ(history.result.value("messages").toArray().first().toObject().value("content").toString(), "saved body");
}

TEST_F(SendRecoveryView, FailedDurableDismissalKeepsVisibleSendUntilExplicitSuccessfulDeletion)
{
    seed(true);
    startClient();
    ASSERT_FALSE(operation().isEmpty());
    daemon.keyring->failRecoveryRead = true;
    app->dismissSend("restored-operation");
    ASSERT_TRUE(waitFor([&] {
        return !app->sendOperations().isEmpty() && !app->sendOperations().first().toMap().value("pending").toBool();
    }));
    EXPECT_TRUE(app->sendOperations().first().toMap().value("error").toString().contains("Cannot delete"));
    EXPECT_EQ(app->sendOperations().first().toMap().value("text").toString(), "saved composer draft");
    daemon.keyring->failRecoveryRead = false;
    EXPECT_EQ(savedRecords(), 1);
    app->dismissSend("restored-operation");
    ASSERT_TRUE(waitFor([&] { return app->sendOperations().isEmpty(); }));
    EXPECT_EQ(savedRecords(), 0);
    EXPECT_EQ(composerRestores, 0);
}

TEST_F(SendRecoveryView, AccountSwitchHidesOtherAccountRecoveryAndRestoresOnlyItsBinding)
{
    seed(true);
    ASSERT_TRUE(daemon.authenticate(server, "recovery-other"));
    ASSERT_TRUE(daemon.call("server.create", {{"name", "Other account"}}).ok);
    const auto otherAccount = daemon.binding().accountId;
    ASSERT_NE(otherAccount, originalBinding.accountId);
    startClient();
    EXPECT_EQ(app->accountId(), otherAccount);
    EXPECT_TRUE(app->sendOperations().isEmpty());
    EXPECT_EQ(app->failedSendCount(), 0);
    app->switchAccount(originalBinding.accountId);
    ASSERT_TRUE(waitFor([&] { return app->accountId() == originalBinding.accountId && app->canSend(); }));
    ASSERT_FALSE(operation().isEmpty());
    EXPECT_EQ(app->selectedChannelId(), channelId);
    EXPECT_EQ(app->sendOperations().first().toMap().value("text").toString(), "saved composer draft");
    app->switchAccount(otherAccount);
    ASSERT_TRUE(waitFor([&] { return app->accountId() == otherAccount && app->canSend(); }));
    EXPECT_TRUE(app->sendOperations().isEmpty());
    EXPECT_EQ(app->failedSendCount(), 0);
    app->retrySend("restored-operation");
    EXPECT_TRUE(app->sendOperations().isEmpty());
    EXPECT_EQ(composerRestores, 0);
}
TEST_F(SendRecoveryView, OfflineRetainsUncertainRecoveryForCurrentIdentityWithoutComposerMutation)
{
    seed();
    startClient();
    const auto restored = operation();
    ASSERT_FALSE(restored.isEmpty());
    const auto account = app->accountId();
    const auto selectedChannel = app->selectedChannelId();
    ASSERT_TRUE(daemon.call("disconnect").ok);
    ASSERT_TRUE(waitFor([&] { return app->state() != "connected"; }));
    EXPECT_EQ(app->accountId(), account);
    EXPECT_EQ(app->selectedChannelId(), selectedChannel);
    ASSERT_EQ(app->sendOperations().size(), 1);
    const auto offline = app->sendOperations().first().toMap();
    EXPECT_EQ(offline.value("id"), restored.value("id"));
    EXPECT_EQ(offline.value("text"), restored.value("text"));
    EXPECT_FALSE(offline.value("pending").toBool());
    EXPECT_FALSE(offline.value("retryable").toBool());
    EXPECT_EQ(app->failedSendCount(), 1);
    app->retrySend("restored-operation");
    QTest::qWait(100);
    EXPECT_EQ(app->sendOperations().first().toMap(), offline);
    EXPECT_EQ(composerRestores, 0);
}
} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("OMACHAT_THEME", "builtin");
    QGuiApplication application(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
