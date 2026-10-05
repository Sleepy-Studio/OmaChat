#include "Harness.hpp"
#include "controllers/AppController.hpp"
#include "platform/ThemeProvider.hpp"
#include <QGuiApplication>
#include <QDir>
#include <QImage>
#include "views/VideoFrameItem.hpp"
#include "views/AudioLevels.hpp"
#include <QFile>
#include <QJsonArray>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTest>
#include <QProcess>
#include <gtest/gtest.h>

using namespace omachat;
using namespace omachat::test;

namespace {
struct Operations : ::testing::Test {
    TestServer server;
    std::unique_ptr<TestDaemon> daemon;
    client::ThemeProvider theme{nullptr};
    std::unique_ptr<client::AppController> app;
    QTemporaryDir files;
    void SetUp() override {
        ASSERT_TRUE(server.start());
        daemon = std::make_unique<TestDaemon>("operations");
        ASSERT_TRUE(daemon->start());
        ASSERT_TRUE(daemon->registerOn(server, "operations", "test-password"));
        ASSERT_TRUE(daemon->call("server.create", {{"name", "Operations"}}).ok);
        qputenv("OMACHAT_SOCKET", QString("/tmp/omachat-test-operations-%1.sock")
            .arg(QCoreApplication::applicationPid()).toUtf8());
        config::ClientConfig config;
        config.startup.launchDaemon = false;
        qputenv("XDG_CACHE_HOME", files.path().toUtf8());
        app = std::make_unique<client::AppController>(config);
        app->start();
        ASSERT_TRUE(waitFor([&] { return app->canSend(); }));
    }
    void TearDown() override { app.reset(); daemon.reset(); qunsetenv("OMACHAT_SOCKET"); qunsetenv("XDG_CACHE_HOME"); }
    QString file(qint64 size = 32) {
        const QString path = files.filePath("upload.bin");
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) return {};
        f.write(QByteArray(size, 'x'));
        return path;
    }
    QVariantMap failed() {
        if (!waitFor([&] { return !app->sendOperations().isEmpty()
            && !app->sendOperations().first().toMap().value("pending").toBool(); })) return {};
        return app->sendOperations().first().toMap();
    }
};
}

TEST_F(Operations, FailedUploadRetainsTextAndRetryDoesNotDuplicate) {
    const QString path = file();
    app->addFiles({QUrl::fromLocalFile(path)});
    ASSERT_TRUE(QFile::remove(path));
    ASSERT_TRUE(app->sendComposer("retained body"));
    auto op = failed();
    ASSERT_FALSE(op.isEmpty());
    EXPECT_EQ(op.value("text").toString(), "retained body");
    EXPECT_TRUE(op.value("retryable").toBool());
    const auto id = op.value("id").toString();
    ASSERT_EQ(file(), path);
    app->retrySend(id);
    app->retrySend(id);
    ASSERT_TRUE(waitFor([&] { return app->sendOperations().isEmpty(); }));
    const auto history = daemon->call("message.history", {{"channel", "general"}});
    ASSERT_TRUE(history.ok);
    EXPECT_EQ(history.result.value("messages").toArray().size(), 1);
    ASSERT_FALSE(app->uploads().isEmpty());
    EXPECT_TRUE(app->uploads().first().toMap().value("complete").toBool());
    app->dismissTransfer(app->uploads().first().toMap().value("key").toString());
    EXPECT_TRUE(app->uploads().isEmpty());
}

TEST_F(Operations, CancellationAndChannelSwitchRetainTheOriginalContext) {
    app->addFiles({QUrl::fromLocalFile(file(24 * 1024 * 1024))});
    const QString channel = app->selectedChannelId();
    ASSERT_TRUE(app->sendComposer("cancel body"));
    ASSERT_TRUE(waitFor([&] { return !app->uploads().isEmpty(); }));
    app->cancelTransfer(app->uploads().first().toMap().value("id").toString());
    const auto op = failed();
    ASSERT_FALSE(op.isEmpty());
    EXPECT_TRUE(op.value("retryable").toBool());
    EXPECT_FALSE(app->uploads().isEmpty());
    app->selectHome();
    EXPECT_TRUE(app->sendOperations().isEmpty());
    EXPECT_TRUE(app->uploads().isEmpty());
    EXPECT_EQ(app->failedSendCount(), 1);
    app->reviewFailedSend();
    EXPECT_EQ(app->selectedChannelId(), channel);
    app->selectHome();
    app->retrySend(op.value("id").toString());
    app->selectChannel(channel);
    ASSERT_EQ(app->sendOperations().size(), 1);
    EXPECT_FALSE(app->sendOperations().first().toMap().value("pending").toBool());
    app->dismissSend(op.value("id").toString());
    EXPECT_TRUE(app->sendOperations().isEmpty());
}

TEST_F(Operations, LostDaemonAcknowledgementDoesNotOfferUnsafeRetry) {
    app->addFiles({QUrl::fromLocalFile(file(24 * 1024 * 1024))});
    ASSERT_TRUE(app->sendComposer("uncertain body"));
    ASSERT_TRUE(waitFor([&] { return !app->uploads().isEmpty(); }));
    daemon.reset();
    const auto op = failed();
    ASSERT_FALSE(op.isEmpty());
    EXPECT_FALSE(op.value("retryable").toBool());
    app->retrySend(op.value("id").toString());
    EXPECT_FALSE(app->sendOperations().first().toMap().value("pending").toBool());
}

TEST_F(Operations, ReadOnlyAndAccountSwitchPreventRetry) {
    const QString ownerAccount = app->accountId();
    const QString ownerChannel = app->selectedChannelId();
    const auto invite = daemon->call("invite.create", {{"server", "Operations"}});
    ASSERT_TRUE(invite.ok);
    ASSERT_TRUE(daemon->registerOn(server, "reader", "reader-password"));
    ASSERT_TRUE(daemon->call("server.join", {{"invite", invite.result.value("uri")}}).ok);
    ASSERT_TRUE(waitFor([&] {
        if (app->selfUsername() != "reader") return false;
        app->selectChannel(ownerChannel);
        return app->canSend();
    }));
    const QString readerAccount = app->accountId();
    const QString channel = app->selectedChannelId();
    const QString path = file();
    app->addFiles({QUrl::fromLocalFile(path)});
    ASSERT_TRUE(QFile::remove(path));
    ASSERT_TRUE(app->sendComposer("reader draft"));
    const auto op = failed();
    ASSERT_FALSE(op.isEmpty());
    TestDaemon moderator("moderator");
    ASSERT_TRUE(moderator.start());
    ASSERT_TRUE(moderator.loginOn(server, "operations", "test-password"));
    ASSERT_TRUE(moderator.call("override.set", {{"channel", "general"}, {"user", "reader"},
        {"deny", QJsonArray{"SEND_MESSAGES"}}}).ok);
    ASSERT_TRUE(waitFor([&] { return !app->canSend(); }));
    app->retrySend(op.value("id").toString());
    EXPECT_FALSE(app->sendOperations().first().toMap().value("pending").toBool());
    EXPECT_FALSE(app->sendComposer("forbidden new draft"));
    app->switchAccount(ownerAccount);
    ASSERT_TRUE(waitFor([&] { return app->accountId() == ownerAccount && app->canSend(); }));
    EXPECT_TRUE(app->sendOperations().isEmpty());
    app->retrySend(op.value("id").toString());
    app->switchAccount(readerAccount);
    ASSERT_TRUE(waitFor([&] { return app->accountId() == readerAccount && !app->canSend(); }));
    app->selectChannel(channel);
    ASSERT_EQ(app->sendOperations().size(), 1);
    EXPECT_FALSE(app->sendOperations().first().toMap().value("pending").toBool());
}

TEST_F(Operations, ReconnectKeepsUploadProgressUntilDelivery) {
    app->addFiles({QUrl::fromLocalFile(file(24 * 1024 * 1024))});
    ASSERT_TRUE(app->sendComposer("reconnect body"));
    ASSERT_TRUE(waitFor([&] { return !app->uploads().isEmpty(); }));
    daemon->daemon().connection().dropLink("operation test");
    ASSERT_TRUE(waitFor([&] { return !app->uploads().isEmpty()
        && app->uploads().first().toMap().value("waiting").toBool(); }));
    EXPECT_FALSE(app->sendOperations().isEmpty());
    ASSERT_TRUE(waitFor([&] { return app->sendOperations().isEmpty(); }, 60000));
    EXPECT_TRUE(app->uploads().first().toMap().value("complete").toBool());
}

TEST_F(Operations, FailedSendControlsRenderAndRetryWithPointerAtLargeScale) {
    const QString path = file();
    app->addFiles({QUrl::fromLocalFile(path)});
    ASSERT_TRUE(QFile::remove(path));
    ASSERT_TRUE(app->sendComposer("visual retained draft"));
    ASSERT_FALSE(failed().isEmpty());
    theme.setScale(1.5);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls
import OmaChat
ApplicationWindow {
    width: 420; height: 460; visible: true; color: Theme.background
    Composer { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom }
}
)", QUrl());
    std::unique_ptr<QObject> object(component.create());
    ASSERT_TRUE(object) << component.errorString().toStdString();
    auto* window = qobject_cast<QQuickWindow*>(object.get());
    ASSERT_TRUE(window);
    ASSERT_TRUE(waitFor([&] { return window->isVisible(); }));
    QTest::qWait(100);
    const auto image = window->grabWindow();
    EXPECT_FALSE(image.isNull());
    EXPECT_TRUE(image.save("/tmp/omachat-desktop-operation-150.png"));
    QQuickItem* retry = nullptr;
    std::function<void(QQuickItem*)> visit = [&](QQuickItem* item) {
        if (item->property("text").toString() == "Retry") retry = item;

        for (auto* child : item->childItems()) visit(child);
    };
    visit(window->contentItem());
    ASSERT_TRUE(retry);
    EXPECT_TRUE(retry->isVisible());
    EXPECT_TRUE(retry->isEnabled());
    const QPointF point = retry->mapToScene(QPointF(retry->width()/2, retry->height()/2));
    EXPECT_GE(point.y(), 0);
    EXPECT_LT(point.y(), window->height());
    ASSERT_EQ(file(), path);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point.toPoint());
    ASSERT_TRUE(waitFor([&] { return app->sendOperations().isEmpty(); }));
    // A second rejected send exercises the same real control by keyboard.
    theme.setScale(1.0);
    app->addFiles({QUrl::fromLocalFile(path)});
    ASSERT_TRUE(QFile::remove(path));
    ASSERT_TRUE(app->sendComposer("keyboard retained draft"));
    ASSERT_FALSE(failed().isEmpty());
    QTest::qWait(100);
    EXPECT_TRUE(window->grabWindow().save("/tmp/omachat-desktop-operation-100.png"));
    retry = nullptr;
    visit(window->contentItem());
    ASSERT_TRUE(retry);
    retry->forceActiveFocus();
    ASSERT_EQ(file(), path);
    QTest::keyClick(window, Qt::Key_Space);
    ASSERT_TRUE(waitFor([&] { return app->sendOperations().isEmpty(); }));
}

namespace {
QQuickItem* namedControl(QQuickItem* root, const QString& name) {
    if (root->objectName() == name && root->isVisible()) return root;
    for (auto* child : root->childItems())
        if (auto* found = namedControl(child, name)) return found;
    return nullptr;
}
QQuickItem* textControl(QQuickItem* root, const QString& text) {
    if (root->property("text").toString() == text && root->isVisible()) return root;
    for (auto* child : root->childItems())
        if (auto* found = textControl(child, text)) return found;
    return nullptr;
}
std::unique_ptr<QObject> editorWindow(QQmlEngine& engine, const QByteArray& type) {
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nimport QtQuick.Controls\nimport OmaChat\n"
        "ApplicationWindow { width: 720; height: 460; visible: true; "
        + type + " { objectName: \"editor\" } }", QUrl());
    auto result = std::unique_ptr<QObject>(component.create());
    if (!result) qWarning() << component.errorString();
    return result;
}
void clickText(QQuickWindow* window, const QString& text) {
    auto* item = textControl(window->contentItem(), text);
    ASSERT_TRUE(item) << text.toStdString();
    ASSERT_TRUE(item->isEnabled());
    const auto point = item->mapToScene(QPointF(item->width()/2, item->height()/2));
    ASSERT_GE(point.y(), 0);
    ASSERT_LT(point.y(), window->height());
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point.toPoint());
}
}

TEST_F(Operations, ChannelCreationRetainsRejectedInputAndOptionalFields) {
    theme.setScale(1.5);
    QQmlEngine engine;
    auto object = editorWindow(engine, "CreateChannelDialog");
    ASSERT_TRUE(object);
    auto* window = qobject_cast<QQuickWindow*>(object.get());
    auto* editor = object->findChild<QObject*>("editor");
    ASSERT_TRUE(editor);
    ASSERT_TRUE(QMetaObject::invokeMethod(editor, "open"));
    QTest::qWait(200);
    EXPECT_FALSE(editor->property("advanced").toBool());
    auto* name = editor->findChild<QObject*>("channelName");
    auto* topic = editor->findChild<QObject*>("channelTopic");
    ASSERT_TRUE(name); ASSERT_TRUE(topic);
    name->setProperty("text", QString("bad") + QChar(1));
    clickText(window, "Optional settings…");
    EXPECT_TRUE(editor->property("advanced").toBool());
    topic->setProperty("text", "retained topic");
    clickText(window, "Create");
    ASSERT_TRUE(waitFor([&] { return !editor->property("saving").toBool(); }));
    EXPECT_TRUE(editor->property("visible").toBool());
    EXPECT_FALSE(editor->property("saveError").toString().isEmpty());
    EXPECT_EQ(topic->property("text").toString(), "retained topic");
    EXPECT_EQ(name->property("text").toString(), QString("bad") + QChar(1));
    EXPECT_TRUE(window->grabWindow().save("/tmp/omachat-channel-create-150.png"));
    name->setProperty("text", "created-channel");
    clickText(window, "Create");
    ASSERT_TRUE(waitFor([&] { return !editor->property("visible").toBool(); }));
    EXPECT_EQ(app->channelDetails(app->selectedChannelId()).value("topic").toString(), "retained topic");
}

TEST_F(Operations, ChannelSettingsGuardEscapeOutsideAndRejectedSave) {
    theme.setScale(1.0);
    QQmlEngine engine;
    auto object = editorWindow(engine, "ChannelSettingsDialog");
    ASSERT_TRUE(object);
    auto* window = qobject_cast<QQuickWindow*>(object.get());
    auto* editor = object->findChild<QObject*>("editor");
    const QString id = app->selectedChannelId();
    ASSERT_TRUE(QMetaObject::invokeMethod(editor, "openFor", Q_ARG(QVariant, QVariant(id))));
    QTest::qWait(200);
    auto* name = editor->findChild<QObject*>("channelName");
    ASSERT_TRUE(name);
    name->setProperty("text", QString("bad") + QChar(1));
    ASSERT_TRUE(editor->property("dirty").toBool());
    clickText(window, "Save");
    ASSERT_TRUE(waitFor([&] { return !editor->property("saving").toBool(); }));
    EXPECT_TRUE(editor->property("saveFailed").toBool());
    EXPECT_TRUE(editor->property("dirty").toBool());
    EXPECT_TRUE(editor->property("visible").toBool());
    QTest::keyClick(window, Qt::Key_Escape);
    QTest::qWait(160);
    auto* confirm = editor->findChild<QObject*>("discardChannel");
    ASSERT_TRUE(confirm);
    EXPECT_TRUE(confirm->property("visible").toBool());
    clickText(window, "Cancel");
    QTest::qWait(160);
    EXPECT_TRUE(editor->property("visible").toBool());
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
    QTest::qWait(160);
    EXPECT_FALSE(confirm->property("visible").toBool());
    EXPECT_TRUE(editor->property("visible").toBool());
    EXPECT_TRUE(editor->property("dirty").toBool());
    ASSERT_TRUE(daemon->call("channel.update", {{"channel", id}, {"name", "remote-name"}}).ok);
    ASSERT_TRUE(waitFor([&] { return app->channelDetails(id).value("name").toString() == "remote-name"; }));
    EXPECT_EQ(name->property("text").toString(), QString("bad") + QChar(1));
    name->setProperty("text", " Saved Channel ");
    clickText(window, "Save");
    ASSERT_TRUE(waitFor([&] { return !editor->property("saving").toBool(); }));
    EXPECT_FALSE(editor->property("dirty").toBool());
    EXPECT_FALSE(editor->property("saveFailed").toBool());
    EXPECT_TRUE(editor->property("visible").toBool());
    EXPECT_EQ(app->channelDetails(id).value("name").toString(), "saved-channel");
    EXPECT_TRUE(window->grabWindow().save("/tmp/omachat-channel-settings-100.png"));
    name->setProperty("text", "discard-me");
    clickText(window, "Close");
    QTest::qWait(160);
    clickText(window, "Discard edits");
    ASSERT_TRUE(waitFor([&] { return !editor->property("visible").toBool(); }));
}

TEST_F(Operations, ServerAndRoleSettingsRetainFailuresAndConfirmDiscard) {
    QQmlEngine engine;
    auto object = editorWindow(engine, "ServerSettingsDialog");
    ASSERT_TRUE(object);
    auto* window = qobject_cast<QQuickWindow*>(object.get());
    auto* editor = object->findChild<QObject*>("editor");
    ASSERT_TRUE(QMetaObject::invokeMethod(editor, "open"));
    QTest::qWait(200);
    editor->setProperty("serverName", QString("bad") + QChar(1));
    editor->setProperty("serverDirty", true);
    ASSERT_TRUE(QMetaObject::invokeMethod(editor, "saveServer"));
    ASSERT_TRUE(waitFor([&] { return !editor->property("serverSaving").toBool(); }));
    EXPECT_TRUE(editor->property("serverFailed").toBool());
    EXPECT_TRUE(editor->property("serverDirty").toBool());
    editor->setProperty("serverName", "Saved server");
    ASSERT_TRUE(QMetaObject::invokeMethod(editor, "saveServer"));
    ASSERT_TRUE(waitFor([&] { return !editor->property("serverSaving").toBool(); }));
    EXPECT_FALSE(editor->property("serverDirty").toBool());
    editor->setProperty("serverName", "Submitted server");
    editor->setProperty("serverDirty", true);
    ASSERT_TRUE(QMetaObject::invokeMethod(editor, "saveServer"));
    editor->setProperty("serverName", "Newer unsaved name");
    ASSERT_TRUE(waitFor([&] { return !editor->property("serverSaving").toBool(); }));
    EXPECT_TRUE(editor->property("serverDirty").toBool());
    EXPECT_EQ(editor->property("serverName").toString(), "Newer unsaved name");
    const auto roles = app->serverRoles();
    ASSERT_FALSE(roles.isEmpty());
    ASSERT_TRUE(QMetaObject::invokeMethod(editor, "selectRole", Q_ARG(QVariant, roles.first()), Q_ARG(QVariant, QVariant(false))));
    editor->setProperty("editPerms", QStringList{"INVALID_PERMISSION"});
    editor->setProperty("dirty", true);
    ASSERT_TRUE(QMetaObject::invokeMethod(editor, "saveRole"));
    ASSERT_TRUE(waitFor([&] { return !editor->property("roleSaving").toBool(); }));
    EXPECT_TRUE(editor->property("roleFailed").toBool());
    EXPECT_TRUE(editor->property("dirty").toBool());
    editor->setProperty("editPerms", roles.first().toMap().value("permissions"));
    ASSERT_TRUE(QMetaObject::invokeMethod(editor, "saveRole"));
    ASSERT_TRUE(waitFor([&] { return !editor->property("roleSaving").toBool(); }));
    EXPECT_FALSE(editor->property("roleFailed").toBool());
    EXPECT_FALSE(editor->property("dirty").toBool());
    ASSERT_TRUE(daemon->call("role.create", {{"server", app->selectedServerId()}, {"name", "Another role"}}).ok);
    ASSERT_TRUE(waitFor([&] { return app->serverRoles().size() > 1; }));
    const QString originalRole = editor->property("roleId").toString();
    QVariant otherRole;
    for (const auto& candidate : app->serverRoles())
        if (candidate.toMap().value("id").toString() != originalRole) otherRole = candidate;
    ASSERT_TRUE(otherRole.isValid());
    editor->setProperty("dirty", true);
    ASSERT_TRUE(QMetaObject::invokeMethod(editor, "requestRole", Q_ARG(QVariant, otherRole)));
    QTest::qWait(160);
    clickText(window, "Cancel");
    QTest::qWait(160);
    EXPECT_EQ(editor->property("roleId").toString(), originalRole);
    EXPECT_TRUE(editor->property("dirty").toBool());
    ASSERT_TRUE(QMetaObject::invokeMethod(editor, "requestRole", Q_ARG(QVariant, otherRole)));
    QTest::qWait(160);
    clickText(window, "Discard edits");
    QTest::qWait(160);
    EXPECT_EQ(editor->property("roleId").toString(), otherRole.toMap().value("id").toString());
    EXPECT_TRUE(editor->property("visible").toBool());
    editor->setProperty("dirty", true);
    QTest::keyClick(window, Qt::Key_Escape);
    QTest::qWait(160);
    auto* confirm = editor->findChild<QObject*>("discardServer");
    ASSERT_TRUE(confirm);
    EXPECT_TRUE(confirm->property("visible").toBool());
    clickText(window, "Cancel");
    QTest::qWait(160);
    EXPECT_TRUE(editor->property("visible").toBool());
    ASSERT_TRUE(QMetaObject::invokeMethod(editor, "requestClose"));
    QTest::qWait(160);
    clickText(window, "Discard edits");
    ASSERT_TRUE(waitFor([&] { return !editor->property("visible").toBool(); }));
}

TEST_F(Operations, ArtworkReservesSpaceThroughDownloadAndDecodeFailures) {
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls
import OmaChat
ApplicationWindow {
    width: 720; height: 460; visible: true
    ArtworkBanner { objectName: "banner"; width: 300; height: 88 }
})", QUrl());
    std::unique_ptr<QObject> object(component.create());
    ASSERT_TRUE(object) << component.errorString().toStdString();
    auto* window = qobject_cast<QQuickWindow*>(object.get());
    auto* banner = object->findChild<QQuickItem*>("banner");
    ASSERT_TRUE(banner);
    QTest::qWait(100);
    ASSERT_TRUE(textControl(window->contentItem(), "Loading artwork…"));
    app->requestPreview("999999999999", "missing.png", 0);
    ASSERT_TRUE(waitFor([&] { return app->previewErrors().contains("999999999999"); }));
    banner->setProperty("failed", true);
    ASSERT_TRUE(textControl(window->contentItem(), "Artwork unavailable"));
    banner->setProperty("failed", false);
    banner->setProperty("source", QUrl::fromLocalFile(files.filePath("missing.png")));
    ASSERT_TRUE(waitFor([&] { return banner->property("imageStatus").toInt() == 3; }));
    EXPECT_TRUE(textControl(window->contentItem(), "Artwork unavailable"));
    QImage image(80, 40, QImage::Format_RGB32);
    image.fill(Qt::blue);
    ASSERT_TRUE(image.save(files.filePath("valid.png")));
    banner->setProperty("source", QUrl::fromLocalFile(files.filePath("valid.png")));
    ASSERT_TRUE(waitFor([&] { return banner->property("imageStatus").toInt() == 1; }));
    EXPECT_FALSE(textControl(window->contentItem(), "Artwork unavailable"));
    EXPECT_DOUBLE_EQ(banner->width(), 300);
    EXPECT_DOUBLE_EQ(banner->height(), 88);
}

TEST_F(Operations, FullWindowConversationKeyboardSearchAndRecoveryAtMinimumSize) {
    const auto seed = daemon->call("message.send", {{"channel", "general"}, {"content", "acceptance needle"}});
    ASSERT_TRUE(seed.ok);
    app->messages()->reload();
    ASSERT_TRUE(waitFor([&] { return app->messages()->rowCount() == 1; })) << app->selectedChannelName().toStdString();
    const QString seedId = app->messages()->get(0).value("id").toString();
    ASSERT_FALSE(seedId.isEmpty());
    for (int i = 0; i < 8; ++i)
        ASSERT_TRUE(daemon->call("message.send", {{"channel", "general"},
            {"content", QString("acceptance other %1").arg(i)}}).ok);
    for (bool light : {false, true}) {
        if (light) {
            QFile colors(files.filePath("light.toml"));
            ASSERT_TRUE(colors.open(QIODevice::WriteOnly));
            colors.write("background = '#fafafa'\nforeground = '#262626'\naccent = '#2563eb'\n");
            colors.close();
            ASSERT_TRUE(theme.loadFile(colors.fileName()));
        }
        theme.setScale(1.5);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QtQuick.Controls
import OmaChat
ApplicationWindow {
    width: 720; height: 460; visible: true
    MainView { anchors.fill: parent; objectName: "mainView" }
})", QUrl());
        std::unique_ptr<QObject> object(component.create());
        ASSERT_TRUE(object) << component.errorString().toStdString();
        auto* window = qobject_cast<QQuickWindow*>(object.get());
        auto* input = object->findChild<QQuickItem*>("composerInput");
        ASSERT_TRUE(input);
        QTest::qWait(250);
        QObject* reaction = nullptr;
        std::function<void(QQuickItem*)> findReaction = [&](QQuickItem* item) {
            if (auto* popup = item->findChild<QObject*>("reactionEmojiPicker")) reaction = popup;
            for (auto* child : item->childItems()) findReaction(child);
        };
        findReaction(window->contentItem());
        ASSERT_TRUE(reaction);
        ASSERT_TRUE(QMetaObject::invokeMethod(reaction, "open"));
        QTest::qWait(100);
        EXPECT_GE(reaction->property("y").toReal(), 0);
        EXPECT_LE(reaction->property("y").toReal() + reaction->property("height").toReal(), window->height());
        QTest::keyClick(window, Qt::Key_Escape);
        ASSERT_TRUE(waitFor([&] { return !reaction->property("visible").toBool(); }));
        input->forceActiveFocus();
        for (char key : QByteArray(light ? "light reply" : "dark reply")) QTest::keyClick(window, key);
        app->startReply(seedId);
        ASSERT_EQ(app->replyToId(), seedId);
        ASSERT_TRUE(waitFor([&] { return input->hasActiveFocus(); }));
        QTest::keyClick(window, Qt::Key_Return);
        ASSERT_TRUE(waitFor([&] { return input->property("text").toString().isEmpty() && app->replyToId().isEmpty(); }));
        QTest::keyClick(window, Qt::Key_F, Qt::ControlModifier);
        auto* search = object->findChild<QObject*>("searchPanel");
        auto* query = object->findChild<QQuickItem*>("searchQuery");
        ASSERT_TRUE(search); ASSERT_TRUE(query);
        ASSERT_TRUE(waitFor([&] { return query->hasActiveFocus(); }));
        for (char key : QByteArray("acceptance")) QTest::keyClick(window, key);
        QTest::keyClick(window, Qt::Key_Return);
        ASSERT_TRUE(waitFor([&] { return !app->searchBusy() && app->searchResults()->rowCount() == 9; }));
        QTest::qWait(100);
        EXPECT_LE(search->property("y").toDouble() + search->property("height").toDouble(), window->height());
        EXPECT_TRUE(window->grabWindow().save(light ? "/tmp/omachat-search-light-150.png" : "/tmp/omachat-search-dark-150.png"));
        query->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        for (char key : QByteArray("acceptance needle")) QTest::keyClick(window, key);
        ASSERT_EQ(query->property("text").toString(), "acceptance needle");
        QTest::keyClick(window, Qt::Key_Return);
        ASSERT_TRUE(waitFor([&] { return !app->searchBusy() && app->searchResults()->rowCount() == 1; }));
        EXPECT_LE(search->property("y").toDouble() + search->property("height").toDouble(), window->height());
        QTest::keyClick(window, Qt::Key_Down);
        QTest::keyClick(window, Qt::Key_Return);
        ASSERT_TRUE(waitFor([&] { return !search->property("visible").toBool() && app->messages()->anchorMessageId() == seedId; }));
        clickText(window, "Return to latest");
        ASSERT_TRUE(waitFor([&] { return app->messages()->anchorMessageId().isEmpty(); }));
        const auto point = input->mapToScene(QPointF(0, 0));
        EXPECT_GE(point.x(), 0); EXPECT_GE(point.y(), 0);
        EXPECT_LE(point.y() + input->height(), window->height());
        const QString missingUpload = file();
        app->addFiles({QUrl::fromLocalFile(missingUpload)});
        ASSERT_TRUE(QFile::remove(missingUpload));
        ASSERT_TRUE(app->sendComposer("window recovery"));
        ASSERT_FALSE(failed().isEmpty());
        EXPECT_TRUE(textControl(window->contentItem(), "Dismiss"));
        app->selectHome();
        ASSERT_TRUE(waitFor([&] { return textControl(window->contentItem(), "Review send"); }));
        clickText(window, "Review send");
        ASSERT_TRUE(waitFor([&] { return app->selectedChannelId().length() > 0 && textControl(window->contentItem(), "Dismiss"); }));
        QTest::qWait(160);
        EXPECT_TRUE(window->grabWindow().save(light ? "/tmp/omachat-main-light-150.png" : "/tmp/omachat-main-dark-150.png"));
        clickText(window, "Dismiss");
        EXPECT_EQ(app->failedSendCount(), 0);
    }
}

TEST_F(Operations, ArtworkCacheSurvivesGuiRestartOfflineAndRevokesReplacedArtwork) {
    const auto status = daemon->call("daemon.status");
    ASSERT_TRUE(status.ok);
    EXPECT_EQ(status.result.value("account").toObject().value("trusted_fingerprint").toString(), server.fingerprint());
    QImage image(80, 40, QImage::Format_RGB32);
    image.fill(Qt::blue);
    const QString imagePath = files.filePath("artwork.png");
    ASSERT_TRUE(image.save(imagePath));
    const QString channel = app->selectedChannelId();
    ASSERT_TRUE(daemon->call("channel.artwork.set", {{"channel", channel}, {"kind", "banner"},
        {"file", imagePath}}).ok);
    ASSERT_TRUE(waitFor([&] { return !app->selectedChannelBannerId().isEmpty()
        && app->selectedChannelBannerId() != "0"; }));
    const QString firstId = app->selectedChannelBannerId();
    app->requestPreview(firstId, "channel-banner.png", 0);
    ASSERT_TRUE(waitFor([&] { return app->previews().contains(firstId); }));
    const QString firstPath = app->previews().value(firstId).toUrl().toLocalFile();
    EXPECT_TRUE(firstPath.contains("/artwork-v1/"));
    EXPECT_FALSE(QImage(firstPath).isNull());
    image.fill(Qt::red);
    ASSERT_TRUE(image.save(imagePath));
    ASSERT_TRUE(daemon->call("channel.artwork.set", {{"channel", channel}, {"kind", "banner"},
        {"file", imagePath}}).ok);
    ASSERT_TRUE(waitFor([&] { return app->selectedChannelBannerId() != firstId; }));
    EXPECT_FALSE(app->previews().contains(firstId));
    EXPECT_FALSE(QFileInfo::exists(firstPath));
    const QString secondId = app->selectedChannelBannerId();
    app->requestPreview(secondId, "channel-banner.png", 0);
    ASSERT_TRUE(waitFor([&] { return app->previews().contains(secondId); }));
    const QString secondPath = app->previews().value(secondId).toUrl().toLocalFile();
    server.stop();
    ASSERT_TRUE(waitFor([&] { return app->state() != "connected"; }));
    app.reset();
    config::ClientConfig config;
    config.startup.launchDaemon = false;
    app = std::make_unique<client::AppController>(config);
    app->start();
    ASSERT_TRUE(waitFor([&] { return app->selectedChannelBannerId() == secondId; }));
    app->requestPreview(secondId, "channel-banner.png", 0);
    ASSERT_TRUE(waitFor([&] { return app->previews().contains(secondId); }));
    EXPECT_EQ(app->previews().value(secondId).toUrl().toLocalFile(), secondPath);
    EXPECT_FALSE(QImage(secondPath).isNull());
}

TEST_F(Operations, CompactAndWideAdministrationEmojiAndDrawerKeyboardAcceptance) {
    TestDaemon member("compact-member");
    ASSERT_TRUE(member.start());
    ASSERT_TRUE(member.registerOn(server, "compact-member", "test-password"));
    ASSERT_TRUE(member.call("profile.update", {{"display_name", QString(32, 'N')},
        {"bio", QString(300, 'b')}}).ok);
    const auto invite = daemon->call("invite.create", {{"server", app->selectedServerId()}});
    ASSERT_TRUE(invite.ok);
    ASSERT_TRUE(member.call("server.join", {{"invite", invite.result.value("uri")}}).ok);
    ASSERT_TRUE(waitFor([&] { return app->serverMembers().size() == 2; }));
    QImage emojiImage(32, 32, QImage::Format_RGB32);
    emojiImage.fill(Qt::blue);
    ASSERT_TRUE(emojiImage.save(files.filePath("custom.png")));
    ASSERT_TRUE(daemon->call("role.create", {{"server", app->selectedServerId()}, {"name", "Keyboard role"}}).ok);
    ASSERT_TRUE(waitFor([&] { return app->serverRoles().size() > 1; }));
    for (bool light : {false, true}) {
        if (light) {
            QFile colors(files.filePath("compact-light.toml"));
            ASSERT_TRUE(colors.open(QIODevice::WriteOnly));
            colors.write("background = '#fafafa'\nforeground = '#262626'\naccent = '#2563eb'\n");
            colors.close();
            ASSERT_TRUE(theme.loadFile(colors.fileName()));
        }
        for (bool compact : {true, false}) {
            app->createServerEmoji("keyboard_icon", QUrl::fromLocalFile(files.filePath("custom.png")));
            ASSERT_TRUE(waitFor([&] { return app->serverEmoji().size() == 1; }));
            theme.setScale(compact ? 1.5 : 1.0);
            QQmlEngine engine;
            QQmlComponent component(&engine);
            component.setData(R"(
import QtQuick
import QtQuick.Controls
import OmaChat
ApplicationWindow {
    width: 720; height: 460; visible: true
    MainView { anchors.fill: parent; objectName: "mainView" }
})", QUrl());
            std::unique_ptr<QObject> object(component.create());
            ASSERT_TRUE(object) << component.errorString().toStdString();
            auto* window = qobject_cast<QQuickWindow*>(object.get());
            if (!compact) window->resize(1440, 900);
            QTest::qWait(200);
            if (qEnvironmentVariableIsSet("OMACHAT_TEST_WAYLAND")) {
                const QString match = QString("pid:%1").arg(QCoreApplication::applicationPid());
                ASSERT_EQ(QProcess::execute("hyprctl", {"eval", QString(
                    "hl.dispatch(hl.dsp.window.float({action='set',window='%1'}))").arg(match)}), 0);
                ASSERT_EQ(QProcess::execute("hyprctl", {"eval", QString(
                    "hl.dispatch(hl.dsp.window.resize({x=%1,y=%2,relative=false,window='%3'}))")
                    .arg(compact ? 720 : 1440).arg(compact ? 460 : 900).arg(match)}), 0);
                // Compositor border rounding can add one logical pixel.
                ASSERT_TRUE(waitFor([&] { return qAbs(window->width() - (compact ? 720 : 1440)) <= 1
                    && qAbs(window->height() - (compact ? 460 : 900)) <= 1; })) << "Native test size: "
                    << window->width() << "x" << window->height();
            }
            auto* view = object->findChild<QObject*>("mainView");
            auto* input = object->findChild<QQuickItem*>("composerInput");
            auto* emoji = object->findChild<QObject*>("emojiPicker");
            ASSERT_TRUE(view); ASSERT_TRUE(input); ASSERT_TRUE(emoji);
            input->forceActiveFocus();
            ASSERT_TRUE(QMetaObject::invokeMethod(emoji, "open"));
            QTest::qWait(100);
            EXPECT_GE(emoji->property("x").toReal(), 0);
            EXPECT_GE(emoji->property("y").toReal(), 0);
            EXPECT_LE(emoji->property("x").toReal() + emoji->property("width").toReal(), window->width());
            EXPECT_LE(emoji->property("y").toReal() + emoji->property("height").toReal(), window->height());
            auto* search = object->findChild<QQuickItem*>("emojiSearch");
            ASSERT_TRUE(search);
            EXPECT_TRUE(search->hasActiveFocus());
            search->setProperty("text", "smile");
            QTest::keyClick(window, Qt::Key_Down);
            auto* grid = object->findChild<QQuickItem*>("unicodeEmojiGrid");
            ASSERT_TRUE(grid);
            EXPECT_TRUE(grid->hasActiveFocus());
            ASSERT_TRUE(waitFor([&] { return grid->property("count").toInt() > 0; }));
            QTest::keyClick(window, Qt::Key_Return);
            ASSERT_TRUE(waitFor([&] { return !emoji->property("visible").toBool(); }));
            EXPECT_FALSE(input->property("text").toString().isEmpty());
            EXPECT_TRUE(input->hasActiveFocus());
            ASSERT_TRUE(QMetaObject::invokeMethod(emoji, "open"));
            QTest::keyClick(window, Qt::Key_Escape);
            ASSERT_TRUE(waitFor([&] { return input->hasActiveFocus(); }));
            input->setProperty("text", "");
            if (compact) {
                QTest::keyClick(window, Qt::Key_L, Qt::ControlModifier);
                auto* drawer = object->findChild<QObject*>("channelsDrawer");
                ASSERT_TRUE(drawer);
                ASSERT_TRUE(waitFor([&] { return drawer->property("opened").toBool(); }));
                QTest::keyClick(window, Qt::Key_Escape);
                ASSERT_TRUE(waitFor([&] { return input->hasActiveFocus(); }));
                ASSERT_TRUE(QMetaObject::invokeMethod(view, "toggleMembers"));
                auto* members = object->findChild<QObject*>("membersDrawer");
                ASSERT_TRUE(members);
                ASSERT_TRUE(waitFor([&] { return members->property("opened").toBool(); }));
                ASSERT_TRUE(window->activeFocusItem());
                ASSERT_TRUE(waitFor([&] {
                    for (auto* item = window->activeFocusItem(); item; item = item->parentItem())
                        if (item->objectName() == "memberList") return true;
                    return false;
                }));
                for (const auto key : {Qt::Key_Menu, Qt::Key_F10}) {
                    QTest::keyClick(window, key, key == Qt::Key_F10 ? Qt::ShiftModifier : Qt::NoModifier);
                    ASSERT_TRUE(waitFor([&] {
                        for (auto* menu : object->findChildren<QObject*>("memberContextMenu"))
                            if (menu->property("visible").toBool()) return true;
                        return false;
                    }));
                    QTest::keyClick(window, Qt::Key_Escape);
                    ASSERT_TRUE(waitFor([&] {
                        for (auto* menu : object->findChildren<QObject*>("memberContextMenu"))
                            if (menu->property("visible").toBool()) return false;
                        return true;
                    }));
                }
                QTest::keyClick(window, Qt::Key_Return);
                ASSERT_TRUE(waitFor([&] {
                    for (auto* profile : object->findChildren<QObject*>("memberProfile"))
                        if (profile->property("visible").toBool()) return true;
                    return false;
                }));
                for (auto* profile : object->findChildren<QObject*>("memberProfile")) {
                    if (!profile->property("visible").toBool()) continue;
                    EXPECT_GE(profile->property("y").toReal(), 0);
                    EXPECT_LE(profile->property("y").toReal() + profile->property("height").toReal(), window->height());
                }
                QTest::keyClick(window, Qt::Key_Escape);
                ASSERT_TRUE(waitFor([&] {
                    for (auto* profile : object->findChildren<QObject*>("memberProfile"))
                        if (profile->property("visible").toBool()) return false;
                    return true;
                }));
                QTest::keyClick(window, Qt::Key_Escape);
                ASSERT_TRUE(waitFor([&] { return input->hasActiveFocus(); }));
            }
            auto* editor = object->findChild<QObject*>("serverSettings");
            ASSERT_TRUE(editor);
            ASSERT_TRUE(QMetaObject::invokeMethod(editor, "open"));
            auto* tabs = object->findChild<QObject*>("serverTabs");
            ASSERT_TRUE(tabs);
            tabs->setProperty("currentIndex", 1);
            QTest::qWait(100);
            EXPECT_EQ(editor->property("compactRoles").toBool(), compact);
            auto* selector = object->findChild<QQuickItem*>(compact ? "compactRoleSelector" : "roleList");
            ASSERT_TRUE(selector);
            selector->forceActiveFocus();
            int roleIndex = 0;
            for (int i = 0; i < app->serverRoles().size(); ++i)
                if (app->serverRoles()[i].toMap().value("name").toString() == "Keyboard role") roleIndex = i;
            if (compact) {
                QTest::keyClick(window, Qt::Key_Space);
                QTest::keyClick(window, Qt::Key_Home);
                for (int i = 0; i < roleIndex; ++i) QTest::keyClick(window, Qt::Key_Down);
                QTest::keyClick(window, Qt::Key_Return);
            } else {
                selector->setProperty("currentIndex", roleIndex);
                QTest::keyClick(window, Qt::Key_Return);
            }
            QTest::qWait(100);
            EXPECT_EQ(editor->property("roleId").toString(), app->serverRoles()[roleIndex].toMap().value("id").toString());
            const QString prefix = QString("/tmp/omachat-admin-%1-%2").arg(light ? "light" : "dark", compact ? "150" : "100");
            EXPECT_TRUE(window->grabWindow().save(prefix + "-roles.png"));
            auto* roleViewport = namedControl(window->contentItem(), "roleScroll");
            ASSERT_TRUE(roleViewport);
            for (const QString name : {"roleSwatch", "rolePermission"}) {
                auto* control = namedControl(window->contentItem(), name);
                ASSERT_TRUE(control);
                control->forceActiveFocus();
                QTest::qWait(50);
                const auto point = control->mapToItem(roleViewport, QPointF(0, 0));
                EXPECT_GE(point.y(), -1);
                EXPECT_LE(point.y() + control->height(), roleViewport->height() + 1);
                QTest::keyClick(window, Qt::Key_Space);
                EXPECT_TRUE(editor->property("dirty").toBool());
                auto* revert = textControl(window->contentItem(), "Revert");
                ASSERT_TRUE(revert);
                revert->forceActiveFocus();
                QTest::keyClick(window, Qt::Key_Space);
                EXPECT_FALSE(editor->property("dirty").toBool());
            }
            EXPECT_TRUE(window->grabWindow().save(prefix + "-permissions.png"));
            tabs->setProperty("currentIndex", 2);
            QTest::qWait(100);
            EXPECT_TRUE(window->grabWindow().save(prefix + "-members.png"));
            QQuickItem* chip = nullptr;
            std::function<void(QQuickItem*)> findChip = [&](QQuickItem* item) {
                if (item->objectName() == "memberRoleChip" && item->isVisible() && item->property("canToggle").toBool()) chip = item;
                for (auto* child : item->childItems()) findChip(child);
            };
            findChip(window->contentItem());
            ASSERT_TRUE(chip);
            const bool held = chip->property("held").toBool();
            const QString memberId = chip->property("memberId").toString();
            const QString assignedRole = chip->property("modelData").toMap().value("id").toString();
            const auto hasRole = [&] {
                for (const auto& row : app->serverMembers()) {
                    const auto data = row.toMap();
                    if (data.value("userId").toString() == memberId) return data.value("roles").toStringList().contains(assignedRole);
                }
                return false;
            };
            chip->forceActiveFocus();
            QTest::keyClick(window, Qt::Key_Space);
            ASSERT_TRUE(waitFor([&] { return hasRole() != held; }));
            chip = nullptr;
            std::function<void(QQuickItem*)> findSameChip = [&](QQuickItem* item) {
                if (item->objectName() == "memberRoleChip" && item->property("memberId").toString() == memberId
                    && item->property("modelData").toMap().value("id").toString() == assignedRole) chip = item;
                for (auto* child : item->childItems()) findSameChip(child);
            };
            ASSERT_TRUE(waitFor([&] { findSameChip(window->contentItem()); return chip != nullptr; }));
            chip->forceActiveFocus();
            QTest::keyClick(window, Qt::Key_Space);
            ASSERT_TRUE(waitFor([&] { return hasRole() == held; }));
            tabs->setProperty("currentIndex", 3);
            QTest::qWait(100);
            auto* upload = textControl(window->contentItem(), "Upload");
            ASSERT_TRUE(upload);
            const QPointF pos = upload->mapToScene(QPointF(0, 0));
            EXPECT_GE(pos.x(), 0); EXPECT_GE(pos.y(), 0);
            EXPECT_LE(pos.x() + upload->width(), window->width());
            EXPECT_LE(pos.y() + upload->height(), window->height());
            EXPECT_TRUE(window->grabWindow().save(prefix + "-emoji.png"));
            auto* removeEmoji = textControl(window->contentItem(), "Delete");
            ASSERT_TRUE(removeEmoji);
            const auto deletePos = removeEmoji->mapToScene(QPointF(0, 0));
            EXPECT_LE(deletePos.y() + removeEmoji->height(), window->height() - 20);
            for (auto* parent = removeEmoji->parentItem(); parent; parent = parent->parentItem()) {
                if (!parent->clip()) continue;
                const auto relative = removeEmoji->mapToItem(parent, QPointF(0, 0));
                EXPECT_GE(relative.y(), 0);
                EXPECT_LE(relative.y() + removeEmoji->height(), parent->height());
            }
            removeEmoji->forceActiveFocus();
            QTest::keyClick(window, Qt::Key_Space);
            ASSERT_TRUE(waitFor([&] { return app->serverEmoji().isEmpty(); }));
            ASSERT_TRUE(QMetaObject::invokeMethod(editor, "close"));
            // Flush surface removal before the next matrix cell uses the same
            // process selector in compositor dispatches.
            window->close();
            QTest::qWait(150);
        }
    }
}

TEST_F(Operations, ArtworkAccountSwitchAndRevokedChannelAccessInvalidateCachedUrls) {
    QImage image(80, 40, QImage::Format_RGB32);
    image.fill(Qt::green);
    const QString imagePath = files.filePath("private-artwork.png");
    ASSERT_TRUE(image.save(imagePath));
    const QString channel = app->selectedChannelId();
    ASSERT_TRUE(daemon->call("channel.artwork.set", {{"channel", channel}, {"kind", "banner"},
        {"file", imagePath}}).ok);
    ASSERT_TRUE(waitFor([&] { return app->selectedChannelBannerId() != "0"
        && !app->selectedChannelBannerId().isEmpty(); }));
    const QString artwork = app->selectedChannelBannerId();
    app->requestPreview(artwork, "banner.png", 0);
    ASSERT_TRUE(waitFor([&] { return app->previews().contains(artwork); }));
    const QString ownerPath = app->previews().value(artwork).toUrl().toLocalFile();
    const auto invite = daemon->call("invite.create", {{"server", app->selectedServerId()}});
    ASSERT_TRUE(invite.ok);
    ASSERT_TRUE(daemon->registerOn(server, "artwork-reader", "test-password"));
    ASSERT_TRUE(daemon->call("server.join", {{"invite", invite.result.value("uri")}}).ok);
    ASSERT_TRUE(waitFor([&] {
        if (app->selfUsername() != "artwork-reader") return false;
        app->selectChannel(channel);
        return app->selectedChannelBannerId() == artwork;
    }));
    EXPECT_FALSE(app->previews().contains(artwork));
    app->requestPreview(artwork, "banner.png", 0);
    ASSERT_TRUE(waitFor([&] { return app->previews().contains(artwork); }));
    const QString readerPath = app->previews().value(artwork).toUrl().toLocalFile();
    EXPECT_NE(readerPath, ownerPath);
    TestDaemon moderator("artwork-moderator");
    ASSERT_TRUE(moderator.start());
    ASSERT_TRUE(moderator.loginOn(server, "operations", "test-password"));
    ASSERT_TRUE(moderator.call("override.set", {{"channel", channel}, {"user", "artwork-reader"},
        {"deny", QJsonArray{"VIEW_CHANNEL"}}}).ok);
    ASSERT_TRUE(waitFor([&] { return app->channels()->indexOf("itemId", channel) < 0; }));
    EXPECT_FALSE(app->previews().contains(artwork));
    EXPECT_FALSE(QFileInfo::exists(readerPath));
}

TEST_F(Operations, LongSearchMetadataStaysInsideCompactResultsAndInvalidActivationIsSafe) {
    for (bool light : {false, true}) {
        if (light) {
            QFile colors(files.filePath("search-light.toml"));
            ASSERT_TRUE(colors.open(QIODevice::WriteOnly));
            colors.write("background = '#fafafa'\nforeground = '#262626'\naccent = '#2563eb'\n");
            colors.close();
            ASSERT_TRUE(theme.loadFile(colors.fileName()));
        }
        theme.setScale(1.5);
        QQmlEngine engine;
        auto object = editorWindow(engine, "SearchPanel");
        ASSERT_TRUE(object);
        auto* window = qobject_cast<QQuickWindow*>(object.get());
        auto* search = object->findChild<QObject*>("editor");
        ASSERT_TRUE(window); ASSERT_TRUE(search);
        ASSERT_TRUE(QMetaObject::invokeMethod(search, "open"));
        QTest::qWait(100);
        search->setProperty("wholeServer", true);
        // Deliberately oversized fixture metadata tests layout; navigation is
        // exercised against real messages by the conversation-flow test.
        app->searchResults()->setRows({{{"itemId", "layout-fixture"}, {"channelId", app->selectedChannelId()},
            {"channel", QString(120, 'c')}, {"author", QString(120, 'a')},
            {"preview", "Bounded search metadata"}, {"time", "10/04/2026 10:20"}}});
        QTest::qWait(100);
        auto* metadata = namedControl(window->contentItem(), "searchMetadata");
        ASSERT_TRUE(metadata);
        for (const QString name : {"searchAuthor", "searchChannel", "searchTime"}) {
            auto* item = namedControl(window->contentItem(), name);
            ASSERT_TRUE(item) << name.toStdString();
            if (!item->isVisible()) continue;
            const auto point = item->mapToItem(metadata, QPointF());
            EXPECT_GE(point.x(), -1);
            EXPECT_LE(point.x() + item->width(), metadata->width() + 1);
        }
        const QString before = app->selectedChannelId();
        ASSERT_TRUE(QMetaObject::invokeMethod(search, "activate", Q_ARG(QVariant, -1)));
        ASSERT_TRUE(QMetaObject::invokeMethod(search, "activate", Q_ARG(QVariant, 1)));
        EXPECT_EQ(app->selectedChannelId(), before);
        EXPECT_TRUE(search->property("visible").toBool());
        EXPECT_TRUE(window->grabWindow().save(light ? "/tmp/omachat-search-long-light-150.png"
                                                   : "/tmp/omachat-search-long-dark-150.png"));
        QTest::keyClick(window, Qt::Key_Escape);
    }
}

TEST_F(Operations, SidebarKeyboardCategoriesAndReorderingUseRealServerState) {
    const auto category = daemon->call("channel.create", {{"server", app->selectedServerId()},
        {"name", "Keyboard category"}, {"type", "category"}});
    ASSERT_TRUE(category.ok);
    const QString categoryId = category.result.value("id").toString();
    const auto child = daemon->call("channel.create", {{"server", app->selectedServerId()},
        {"name", "keyboard-child"}, {"type", "text"}, {"parent", categoryId}});
    ASSERT_TRUE(child.ok);
    const QString childId = child.result.value("id").toString();
    const auto second = daemon->call("channel.create", {{"server", app->selectedServerId()},
        {"name", "keyboard-second"}, {"type", "text"}, {"parent", categoryId}});
    ASSERT_TRUE(second.ok);
    const QString secondId = second.result.value("id").toString();
    ASSERT_TRUE(waitFor([&] { return app->channels()->indexOf("itemId", secondId) >= 0; }));
    theme.setScale(1.5);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls
import OmaChat
ApplicationWindow {
    width: 400; height: 460; visible: true
    ChannelSidebar { anchors.fill: parent }
})", QUrl());
    std::unique_ptr<QObject> object(component.create());
    ASSERT_TRUE(object) << component.errorString().toStdString();
    auto* window = qobject_cast<QQuickWindow*>(object.get());
    auto* list = object->findChild<QQuickItem*>("channelList");
    ASSERT_TRUE(window); ASSERT_TRUE(list);
    QTest::qWait(100);
    list->setProperty("currentIndex", app->channels()->indexOf("itemId", categoryId));
    list->forceActiveFocus();
    const QString selected = app->selectedChannelId();
    QTest::keyClick(window, Qt::Key_Return);
    ASSERT_TRUE(waitFor([&] { return app->channels()->indexOf("itemId", childId) < 0; }));
    EXPECT_EQ(app->selectedChannelId(), selected);
    QTest::keyClick(window, Qt::Key_Enter);
    ASSERT_TRUE(waitFor([&] { return app->channels()->indexOf("itemId", childId) >= 0; }));
    QTest::keyClick(window, Qt::Key_Menu);
    auto* categoryMenu = object->findChild<QObject*>("categoryContextMenu");
    ASSERT_TRUE(categoryMenu);
    ASSERT_TRUE(waitFor([&] { return categoryMenu->property("visible").toBool(); }));
    QTest::keyClick(window, Qt::Key_Escape);
    ASSERT_TRUE(waitFor([&] { return list->hasActiveFocus(); }));
    list->setProperty("currentIndex", app->channels()->indexOf("itemId", childId));
    QTest::keyClick(window, Qt::Key_F10, Qt::ShiftModifier);
    auto* channelMenu = object->findChild<QObject*>("channelContextMenu");
    ASSERT_TRUE(channelMenu);
    ASSERT_TRUE(waitFor([&] { return channelMenu->property("visible").toBool(); }));
    auto* move = textControl(window->contentItem(), "Move down");
    ASSERT_TRUE(move); ASSERT_TRUE(move->isEnabled());
    move->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_Return);
    ASSERT_TRUE(waitFor([&] { return app->channels()->indexOf("itemId", childId)
        > app->channels()->indexOf("itemId", secondId); }));
    const auto snapshot = daemon->call("state.snapshot");
    ASSERT_TRUE(snapshot.ok);
    int firstPosition = -1, secondPosition = -1;
    for (const auto& value : snapshot.result.value("channels").toArray()) {
        const auto row = value.toObject();
        if (row.value("id").toString() == childId) firstPosition = row.value("position").toInt();
        if (row.value("id").toString() == secondId) secondPosition = row.value("position").toInt();
    }
    EXPECT_GT(firstPosition, secondPosition);
    list->setProperty("currentIndex", app->channels()->indexOf("itemId", categoryId));
    list->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_Menu);
    ASSERT_TRUE(waitFor([&] { return categoryMenu->property("visible").toBool(); }));
    auto* remove = textControl(window->contentItem(), "Delete category");
    ASSERT_TRUE(remove);
    remove->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_Return);
    auto* confirmation = object->findChild<QObject*>("categoryDeleteConfirmation");
    ASSERT_TRUE(confirmation);
    ASSERT_TRUE(waitFor([&] { return confirmation->property("visible").toBool(); }));
    ASSERT_TRUE(waitFor([&] { return window->activeFocusItem()
        && window->activeFocusItem()->property("text").toString() == "Cancel"; }));
    QTest::keyClick(window, Qt::Key_Return);
    ASSERT_TRUE(waitFor([&] { return !confirmation->property("visible").toBool(); }));
    EXPECT_GE(app->channels()->indexOf("itemId", categoryId), 0);
    EXPECT_GE(app->channels()->indexOf("itemId", childId), 0);
}

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("OMACHAT_TEST_WAYLAND"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("OMACHAT_THEME", "builtin");
    QGuiApplication app(argc, argv);
    qmlRegisterSingletonType<client::AppController>("OmaChat", 1, 0, "App", &client::AppController::create);
    qmlRegisterSingletonType<client::ThemeProvider>("OmaChat", 1, 0, "Theme", &client::ThemeProvider::create);
    const QString root = QStringLiteral(OMACHAT_SOURCE_ROOT) + "/client/qml/";
    // Runtime URL registration lacks the generated module's implicit imports.
    // Use the actual Metrics source with its module import made explicit.
    QTemporaryDir qmlDir;
    QFile metricsSource(root + "Metrics.qml");
    if (!metricsSource.open(QIODevice::ReadOnly)) return 1;
    QFile metricsCopy(qmlDir.filePath("Metrics.qml"));
    if (!metricsCopy.open(QIODevice::WriteOnly)) return 1;
    metricsCopy.write(metricsSource.readAll().replace("import QtQml", "import QtQml\nimport OmaChat"));
    metricsCopy.close();
    qmlRegisterSingletonType(QUrl::fromLocalFile(metricsCopy.fileName()), "OmaChat", 1, 0, "Metrics");
    qmlRegisterType<client::VideoFrameItem>("OmaChat", 1, 0, "VideoFrameItem");
    qmlRegisterType<client::AudioLevels>("OmaChat", 1, 0, "AudioLevels");
    for (const QString directory : {"components", "views", "dialogs", "pages"}) {
        const QDir dir(root + directory);
        for (const auto& name : dir.entryList({"*.qml"}, QDir::Files)) {
            const auto type = name.chopped(4).toUtf8();
            qmlRegisterType(QUrl::fromLocalFile(dir.filePath(name)), "OmaChat", 1, 0, type.constData());
        }
    }
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
