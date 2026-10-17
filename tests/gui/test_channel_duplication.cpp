#include "Harness.hpp"
#include "controllers/AppController.hpp"
#include "omachat/core/Permissions.hpp"
#include "platform/ThemeProvider.hpp"
#include "views/AudioLevels.hpp"
#include "views/VideoFrameItem.hpp"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTest>
#include <QThreadPool>
#include <gtest/gtest.h>

using namespace omachat;
using namespace omachat::test;

namespace {
class ChannelDuplication : public ::testing::Test {
protected:
    TestServer server;
    TestDaemon daemon{"duplication-ui"};
    client::ThemeProvider theme{nullptr};
    std::unique_ptr<client::AppController> app;
    QTemporaryDir files;
    QQmlEngine engine;
    std::unique_ptr<QObject> object;
    QQuickWindow* window = nullptr;
    QObject* editor = nullptr;
    QString id;
    void SetUp() override
    {
        ASSERT_TRUE(server.start());
        ASSERT_TRUE(daemon.start());
        ASSERT_TRUE(daemon.registerOn(server, "duplication-ui", "test-password"));
        ASSERT_TRUE(daemon.call("server.create", {{"name", "Duplication UI"}}).ok);
        qputenv("OMACHAT_SOCKET",
            QString("/tmp/omachat-test-duplication-ui-%1.sock").arg(QCoreApplication::applicationPid()).toUtf8());
        qputenv("XDG_CACHE_HOME", files.path().toUtf8());
        config::ClientConfig config;
        config.startup.launchDaemon = false;
        app = std::make_unique<client::AppController>(config);
        app->start();
        ASSERT_TRUE(waitFor([&] { return app->canSend(); }));
        id = app->selectedChannelId();
        theme.setReducedMotion(true);
        QQmlComponent component(&engine);
        component.setData(R"QML(import QtQuick
import QtQuick.Controls
import OmaChat
ApplicationWindow { width:720; height:460; visible:true; color: Theme.background
    DuplicateChannelDialog { objectName:"editor" } }
)QML",
            QUrl());
        object.reset(component.create());
        ASSERT_TRUE(object) << component.errorString().toStdString();
        window = qobject_cast<QQuickWindow*>(object.get());
        ASSERT_TRUE(window);
        editor = object->findChild<QObject*>("editor");
        ASSERT_TRUE(editor);
    }
    void TearDown() override
    {
        object.reset();
        app.reset();
        qunsetenv("OMACHAT_SOCKET");
        qunsetenv("XDG_CACHE_HOME");
    }
    QQuickItem* item(const QString& name)
    {
        std::function<QQuickItem*(QQuickItem*)> find = [&](QQuickItem* current) -> QQuickItem* {
            if (current->objectName() == name)
                return current;
            for (auto* child : current->childItems())
                if (auto* found = find(child))
                    return found;
            return nullptr;
        };
        return find(window->contentItem());
    }
    void open()
    {
        ASSERT_TRUE(QMetaObject::invokeMethod(editor, "openFor", Q_ARG(QVariant, id)));
        ASSERT_TRUE(waitFor([&] { return editor->property("opened").toBool(); }));
    }
};

TEST_F(ChannelDuplication, NameAndConfigurationCopyUsesRealDaemon)
{
    ASSERT_TRUE(daemon
            .call("channel.update", {{"channel", id}, {"topic", "Copied topic"}, {"description", "Copied description"}})
            .ok);
    ASSERT_TRUE(waitFor([&] { return app->channelDetails(id).value("topic").toString() == "Copied topic"; }));
    open();
    ASSERT_TRUE(item("duplicateChannelName"));
    item("duplicateChannelName")->setProperty("text", "copy-created");
    ASSERT_TRUE(item("duplicateChannelSubmit")->isEnabled());
    ASSERT_TRUE(QMetaObject::invokeMethod(editor, "submit"));
    ASSERT_TRUE(waitFor([&] { return !editor->property("visible").toBool(); }));
    auto copied = daemon.call("channel.list");
    ASSERT_TRUE(copied.ok);
    bool found = false;
    for (const auto& value : copied.result.value("channels").toArray()) {
        const auto channel = value.toObject();
        if (channel.value("name").toString() == "copy-created") {
            found = true;
            EXPECT_EQ(channel.value("topic").toString(), "Copied topic");
            EXPECT_EQ(channel.value("description").toString(), "Copied description");
            EXPECT_NE(channel.value("id").toString(), id);
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(ChannelDuplication, RejectedNameRetainsDraftAndAllowsCorrection)
{
    open();
    item("duplicateChannelName")->setProperty("text", "   ");
    EXPECT_FALSE(item("duplicateChannelSubmit")->isEnabled());
    item("duplicateChannelName")->setProperty("text", "corrected-copy");
    editor->setProperty("saving", true);
    emit app->administrationFinished("channel.duplicate", id, "Permission denied");
    EXPECT_TRUE(editor->property("visible").toBool());
    EXPECT_EQ(item("duplicateChannelName")->property("text").toString(), "corrected-copy");
    EXPECT_FALSE(editor->property("saving").toBool());
    EXPECT_FALSE(editor->property("error").toString().isEmpty());
}

TEST_F(ChannelDuplication, DisconnectDuringCreationBlocksAnotherAttempt)
{
    open();
    editor->setProperty("saving", true);
    ASSERT_TRUE(daemon.call("disconnect").ok);
    ASSERT_TRUE(waitFor([&] { return app->state() != "connected"; }));
    EXPECT_TRUE(editor->property("uncertain").toBool());
    EXPECT_FALSE(item("duplicateChannelSubmit")->isEnabled());
    EXPECT_FALSE(editor->property("error").toString().isEmpty());
}

TEST_F(ChannelDuplication, RealPermissionLossDisablesRetainedDraftAndDeletionClosesDialog)
{
    RawClient owner(server);
    ASSERT_TRUE(owner.connect());
    ASSERT_TRUE(owner.hello());
    ASSERT_TRUE(owner.registerUser("other-duplication-owner", "test-password"));
    proto::Envelope request;
    request.mutable_create_server()->set_name("Permission controlled source");
    auto reply = owner.call(request);
    ASSERT_TRUE(reply && reply->has_server());
    const auto serverId = reply->server().id();
    request.Clear();
    request.mutable_create_invite()->set_server_id(serverId);
    reply = owner.call(request);
    ASSERT_TRUE(reply && reply->has_invite());
    ASSERT_TRUE(daemon.call("server.join", {{"invite", QString::fromStdString(reply->invite().token())}}).ok);
    request.Clear();
    request.mutable_create_channel()->set_server_id(serverId);
    request.mutable_create_channel()->set_name("managed-source");
    request.mutable_create_channel()->set_type(proto::CHANNEL_TYPE_TEXT);
    reply = owner.call(request);
    ASSERT_TRUE(reply && reply->has_channel());
    id = QString::number(reply->channel().id());
    const auto sourceId = reply->channel().id();
    request.Clear();
    auto* override = request.mutable_set_override()->mutable_override();
    override->set_channel_id(sourceId);
    override->set_target_type(proto::PermissionOverride::TARGET_USER);
    override->set_target_id(app->selfId().toULongLong());
    override->set_allow(permissions::ManageChannel);
    ASSERT_TRUE(owner.call(request)->has_ok());
    ASSERT_TRUE(waitFor([&] { return app->channelDetails(id).value("can_manage").toBool(); }));
    app->selectServer(QString::number(serverId));
    open();
    item("duplicateChannelName")->setProperty("text", "retained-name");
    ASSERT_TRUE(item("duplicateChannelSubmit")->isEnabled());
    override->set_allow(0);
    override->set_deny(permissions::ManageChannel);
    ASSERT_TRUE(owner.call(request)->has_ok());
    ASSERT_TRUE(waitFor([&] { return !item("duplicateChannelSubmit")->isEnabled(); }));
    EXPECT_TRUE(editor->property("visible").toBool());
    EXPECT_EQ(item("duplicateChannelName")->property("text").toString(), "retained-name");
    request.Clear();
    request.mutable_delete_channel()->set_channel_id(sourceId);
    ASSERT_TRUE(owner.call(request)->has_ok());
    ASSERT_TRUE(waitFor([&] { return !editor->property("visible").toBool(); }));
}

class ChannelDuplicationLayout : public ChannelDuplication, public ::testing::WithParamInterface<bool> {
protected:
    void SetUp() override
    {
        ChannelDuplication::SetUp();
        if (HasFatalFailure())
            return;
        if (GetParam()) {
            QFile colors(files.filePath("duplication-light.toml"));
            ASSERT_TRUE(colors.open(QIODevice::WriteOnly));
            colors.write("background = '#fafafa'\nforeground = '#262626'\naccent = '#2563eb'\n");
            colors.close();
            ASSERT_TRUE(theme.loadFile(colors.fileName()));
            emit theme.changed();
            ASSERT_GT(theme.background().lightnessF(), 0.8);
        }
    }
    void verifyLayout(bool compact)
    {
        theme.setScale(compact ? 1.5 : 1.0);
        const int width = compact ? 720 : 1440;
        const int height = compact ? 460 : 900;
        window->resize(width, height);
        QTest::qWait(100);
        if (qEnvironmentVariableIsSet("OMACHAT_TEST_WAYLAND")) {
            const QString match = QString("pid:%1").arg(QCoreApplication::applicationPid());
            ASSERT_EQ(QProcess::execute("hyprctl",
                          {"eval", QString("hl.dispatch(hl.dsp.window.float({action='set',window='%1'}))").arg(match)}),
                0);
            ASSERT_EQ(QProcess::execute("hyprctl",
                          {"eval",
                              QString("hl.dispatch(hl.dsp.window.resize({x=%1,y=%2,relative=false,window='%3'}))")
                                  .arg(width)
                                  .arg(height)
                                  .arg(match)}),
                0);
            ASSERT_EQ(
                QProcess::execute("hyprctl", {"eval", QString("hl.dispatch(hl.dsp.focus({window='%1'}))").arg(match)}),
                0);
            QTest::qWait(100);
        }
        ASSERT_EQ(window->width(), width);
        ASSERT_EQ(window->height(), height);
        open();
        QTest::qWait(100);
        auto* field = item("duplicateChannelName");
        ASSERT_TRUE(field);
        auto* input = qobject_cast<QQuickItem*>(field->property("input").value<QObject*>());
        ASSERT_TRUE(input);
        auto* submit = item("duplicateChannelSubmit");
        ASSERT_TRUE(submit);
        QQuickItem* close = nullptr;
        for (auto* child : submit->parentItem()->childItems())
            if (child->property("text").toString() == "Close" && child->property("primary").isValid())
                close = child;
        ASSERT_TRUE(close);
        for (auto* control : {input, submit, close}) {
            ASSERT_TRUE(control->isVisible());
            ASSERT_TRUE(control->isEnabled());
            const QRectF bounds = control->mapRectToScene(QRectF(0, 0, control->width(), control->height()));
            EXPECT_GT(bounds.width(), 0);
            EXPECT_GT(bounds.height(), 0);
            EXPECT_GE(bounds.left(), 0);
            EXPECT_GE(bounds.top(), 0);
            EXPECT_LE(bounds.right(), window->width());
            EXPECT_LE(bounds.bottom(), window->height());
            for (auto* ancestor = control->parentItem(); ancestor; ancestor = ancestor->parentItem()) {
                if (!ancestor->clip())
                    continue;
                const auto viewport = ancestor->mapRectToScene(QRectF(0, 0, ancestor->width(), ancestor->height()));
                EXPECT_TRUE(viewport.adjusted(-1, -1, 1, 1).contains(bounds))
                    << "Control is clipped by an ancestor viewport";
            }
            control->forceActiveFocus();
            EXPECT_EQ(window->activeFocusItem(), control);
        }
        input->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        for (const char letter : QByteArray(compact ? "compact-copy" : "wide-copy"))
            QTest::keyClick(window, letter);
        EXPECT_EQ(field->property("text").toString(), compact ? "compact-copy" : "wide-copy");
        const QString captureDir = qEnvironmentVariable("OMACHAT_CAPTURE_DIR");
        if (!captureDir.isEmpty()) {
            ASSERT_TRUE(QDir().mkpath(captureDir));
            const QString path = QDir(captureDir)
                                     .filePath(QString("channel-duplicate-%1-%2.png")
                                             .arg(GetParam() ? "light" : "dark", compact ? "compact" : "wide"));
            EXPECT_TRUE(window->grabWindow().save(path));
        }
        close->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Space);
        ASSERT_TRUE(waitFor([&] { return !editor->property("visible").toBool(); }));
    }
};

TEST_P(ChannelDuplicationLayout, CompactScaledControlsRemainReachable)
{
    verifyLayout(true);
}

TEST_P(ChannelDuplicationLayout, WideControlsRemainReachable)
{
    verifyLayout(false);
}

INSTANTIATE_TEST_SUITE_P(Themes, ChannelDuplicationLayout, ::testing::Bool(),
    [](const auto& testCase) { return testCase.param ? "Light" : "Dark"; });

} // namespace
int main(int argc, char** argv)
{
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
    if (!metricsSource.open(QIODevice::ReadOnly))
        return 1;
    QFile metricsCopy(qmlDir.filePath("Metrics.qml"));
    if (!metricsCopy.open(QIODevice::WriteOnly))
        return 1;
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
