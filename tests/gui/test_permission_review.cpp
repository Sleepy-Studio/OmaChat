#include "Harness.hpp"
#include "controllers/AppController.hpp"
#include "platform/ThemeProvider.hpp"
#include "views/AudioLevels.hpp"
#include "views/VideoFrameItem.hpp"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <gtest/gtest.h>

using namespace omachat;
using namespace omachat::test;

namespace {
QQuickItem* namedControl(QQuickItem* root, const QString& name)
{
    if (root->objectName() == name && root->isVisible())
        return root;
    for (auto* child : root->childItems())
        if (auto* found = namedControl(child, name))
            return found;
    return nullptr;
}
bool allowed(const QVariantMap& review, const QString& permission)
{
    for (const auto& row : review.value("rows").toList())
        if (row.toMap().value("name").toString() == permission)
            return row.toMap().value("allowed").toBool();
    return false;
}
struct PermissionReview : ::testing::Test {
    TestServer server;
    TestDaemon owner{"permission-owner"};
    TestDaemon reader{"permission-reader"};
    client::ThemeProvider theme{nullptr};
    std::unique_ptr<client::AppController> app;
    QString category, child;
    QTemporaryDir cache;
    void SetUp() override
    {
        ASSERT_TRUE(server.start());
        ASSERT_TRUE(owner.start());
        ASSERT_TRUE(owner.registerOn(server, "permission-owner", "test-password"));
        const auto created = owner.call("server.create", {{"name", "Permission review"}});
        ASSERT_TRUE(created.ok);
        const auto cat = owner.call(
            "channel.create", {{"server", "Permission review"}, {"name", "restricted"}, {"type", "category"}});
        ASSERT_TRUE(cat.ok);
        category = cat.result.value("id").toString();
        const auto channel = owner.call("channel.create",
            {{"server", "Permission review"}, {"name", "child"}, {"type", "text"}, {"parent", category}});
        ASSERT_TRUE(channel.ok);
        child = channel.result.value("id").toString();
        const auto invite = owner.call("invite.create", {{"server", "Permission review"}});
        ASSERT_TRUE(invite.ok);
        ASSERT_TRUE(reader.start());
        ASSERT_TRUE(reader.registerOn(server, "permission-reader", "test-password"));
        ASSERT_TRUE(reader.call("server.join", {{"invite", invite.result.value("uri")}}).ok);
        qputenv("OMACHAT_SOCKET",
            QString("/tmp/omachat-test-permission-reader-%1.sock").arg(QCoreApplication::applicationPid()).toUtf8());
        config::ClientConfig config;
        config.startup.launchDaemon = false;
        qputenv("XDG_CACHE_HOME", cache.path().toUtf8());
        app = std::make_unique<client::AppController>(config);
        app->start();
        ASSERT_TRUE(waitFor([&] { return app->channelPermissionReview(child).value("available").toBool(); }));
        app->selectChannel(child);
    }
    void TearDown() override
    {
        app.reset();
        qunsetenv("OMACHAT_SOCKET");
        qunsetenv("XDG_CACHE_HOME");
    }
};
} // namespace

TEST_F(PermissionReview, ServerResultIncludesInheritedDenialsAndMemberAllowAndRevocation)
{
    ASSERT_TRUE(owner
            .call("override.set",
                {{"channel", category}, {"user", "permission-reader"},
                    {"deny", QJsonArray{"SEND_MESSAGES", "ATTACH_FILES", "READ_HISTORY"}}})
            .ok);
    ASSERT_TRUE(waitFor([&] { return !allowed(app->channelPermissionReview(child), "SEND_MESSAGES"); }));
    auto review = app->channelPermissionReview(child);
    EXPECT_FALSE(allowed(review, "ATTACH_FILES"));
    EXPECT_FALSE(allowed(review, "READ_HISTORY"));
    EXPECT_TRUE(review.value("inheritance").toString().contains("restricted"));
    EXPECT_FALSE(reader.call("message.send", {{"channel", child}, {"content", "denied"}}).ok);
    EXPECT_FALSE(reader.call("message.history", {{"channel", child}}).ok);
    ASSERT_TRUE(owner
            .call("override.set",
                {{"channel", child}, {"user", "permission-reader"}, {"allow", QJsonArray{"SEND_MESSAGES"}}})
            .ok);
    ASSERT_TRUE(waitFor([&] { return allowed(app->channelPermissionReview(child), "SEND_MESSAGES"); }));
    EXPECT_FALSE(allowed(app->channelPermissionReview(child), "READ_HISTORY"));
    EXPECT_TRUE(reader.call("message.send", {{"channel", child}, {"content", "allowed"}}).ok);
    ASSERT_TRUE(owner.call("override.set", {{"channel", child}, {"user", "permission-reader"}, {"remove", true}}).ok);
    ASSERT_TRUE(waitFor([&] { return !allowed(app->channelPermissionReview(child), "SEND_MESSAGES"); }));
    EXPECT_FALSE(reader.call("message.send", {{"channel", child}, {"content", "revoked"}}).ok);
}

TEST_F(PermissionReview, OwnerBypassesOverrides)
{
    ASSERT_TRUE(owner
            .call("override.set",
                {{"channel", child}, {"user", "permission-owner"},
                    {"deny", QJsonArray{"SEND_MESSAGES", "ATTACH_FILES", "READ_HISTORY"}}})
            .ok);
    app.reset();
    qputenv("OMACHAT_SOCKET",
        QString("/tmp/omachat-test-permission-owner-%1.sock").arg(QCoreApplication::applicationPid()).toUtf8());
    config::ClientConfig config;
    config.startup.launchDaemon = false;
    app = std::make_unique<client::AppController>(config);
    app->start();
    ASSERT_TRUE(waitFor([&] { return app->channelPermissionReview(child).value("available").toBool(); }));
    const auto review = app->channelPermissionReview(child);
    EXPECT_TRUE(allowed(review, "SEND_MESSAGES"));
    EXPECT_TRUE(allowed(review, "ATTACH_FILES"));
    EXPECT_TRUE(allowed(review, "READ_HISTORY"));
    EXPECT_TRUE(owner.call("message.send", {{"channel", child}, {"content", "owner bypass"}}).ok);
}

TEST_F(PermissionReview, ProductionDialogReadbackAndOfflineState)
{
    ASSERT_TRUE(owner
            .call("override.set",
                {{"channel", child}, {"user", "permission-reader"}, {"deny", QJsonArray{"ATTACH_FILES"}}})
            .ok);
    ASSERT_TRUE(waitFor([&] { return !allowed(app->channelPermissionReview(child), "ATTACH_FILES"); }));
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls
import OmaChat
ApplicationWindow {
    width: 720; height: 460; visible: true
    ChannelPermissionsDialog { objectName: "permissions" }
})",
        QUrl());
    std::unique_ptr<QObject> object(component.create());
    ASSERT_TRUE(object) << component.errorString().toStdString();
    auto* dialog = object->findChild<QObject*>("permissions");
    ASSERT_TRUE(dialog);
    ASSERT_TRUE(QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, child), Q_ARG(QVariant, "child")));
    auto* window = qobject_cast<QQuickWindow*>(object.get());
    ASSERT_TRUE(window);
    ASSERT_TRUE(
        waitFor([&] { return namedControl(window->contentItem(), "effectivePermission_ATTACH_FILES") != nullptr; }));
    auto* denied = namedControl(window->contentItem(), "effectivePermission_ATTACH_FILES");
    EXPECT_EQ(denied->property("text").toString(), "Denied");
    EXPECT_FALSE(dialog->property("editingOverrides").toBool());
    EXPECT_FALSE(object->findChild<QObject*>("permissionOverridesTab")->property("enabled").toBool());
    EXPECT_FALSE(app->channelPermissionReview("9999999999999").value("available").toBool());
    server.stop();
    ASSERT_TRUE(waitFor([&] { return !app->channelPermissionReview(child).value("available").toBool(); }));
    ASSERT_TRUE(waitFor([&] { return dialog->property("review").toMap().value("rows").toList().isEmpty(); }));
    EXPECT_TRUE(object->findChild<QObject*>("permissionReviewSource")->property("text").toString().contains("Connect"));
}

TEST_F(PermissionReview, ReadbackFitsCompactAndWideDarkLightWindows)
{
    QTemporaryDir files;
    for (bool light : {false, true}) {
        if (light) {
            QFile colors(files.filePath("permission-light.toml"));
            ASSERT_TRUE(colors.open(QIODevice::WriteOnly));
            colors.write("background = '#fafafa'\nforeground = '#262626'\naccent = '#2563eb'\n");
            colors.close();
            ASSERT_TRUE(theme.loadFile(colors.fileName()));
        }
        for (bool compact : {true, false}) {
            theme.setScale(compact ? 1.5 : 1.0);
            QQmlEngine engine;
            QQmlComponent component(&engine);
            component.setData(R"(
import QtQuick
import QtQuick.Controls
import OmaChat
ApplicationWindow { width: 720; height: 460; visible: true
    ChannelPermissionsDialog { objectName: "permissions" }
})",
                QUrl());
            std::unique_ptr<QObject> object(component.create());
            ASSERT_TRUE(object) << component.errorString().toStdString();
            auto* window = qobject_cast<QQuickWindow*>(object.get());
            ASSERT_TRUE(window);
            if (!compact)
                window->resize(1440, 900);
            QTest::qWait(150);
            if (qEnvironmentVariableIsSet("OMACHAT_TEST_WAYLAND")) {
                const QString match = QString("pid:%1").arg(QCoreApplication::applicationPid());
                ASSERT_EQ(
                    QProcess::execute("hyprctl",
                        {"eval", QString("hl.dispatch(hl.dsp.window.float({action='set',window='%1'}))").arg(match)}),
                    0);
                ASSERT_EQ(QProcess::execute("hyprctl",
                              {"eval",
                                  QString("hl.dispatch(hl.dsp.window.resize({x=%1,y=%2,relative=false,window='%3'}))")
                                      .arg(compact ? 720 : 1440)
                                      .arg(compact ? 460 : 900)
                                      .arg(match)}),
                    0);
            }
            auto* dialog = object->findChild<QObject*>("permissions");
            ASSERT_TRUE(dialog);
            ASSERT_TRUE(QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, child), Q_ARG(QVariant, "child")));
            ASSERT_TRUE(waitFor([&] { return dialog->property("visible").toBool(); }));
            QTest::qWait(180);
            auto* tab = object->findChild<QQuickItem*>("permissionOverridesTab");
            ASSERT_TRUE(tab);
            const QPointF point = tab->mapToScene(QPointF(tab->width() / 2, tab->height() / 2));
            EXPECT_GE(point.x(), 0);
            EXPECT_LT(point.x(), window->width());
            EXPECT_GE(point.y(), 0);
            EXPECT_LT(point.y(), window->height());
            EXPECT_TRUE(window->grabWindow().save(QString("/tmp/omachat-permissions-%1-%2.png")
                    .arg(light ? "light" : "dark", compact ? "compact" : "wide")));
            auto* scroll = object->findChild<QQuickItem*>("effectivePermissionReview");
            ASSERT_TRUE(scroll);
            scroll->forceActiveFocus();
            QTest::keyClick(window, Qt::Key_End);
            auto* view = scroll->property("contentItem").value<QQuickItem*>();
            ASSERT_TRUE(view);
            if (compact) {
                EXPECT_GT(view->property("contentY").toDouble(), 0);
            }
            QTest::qWait(80);
            EXPECT_TRUE(window->grabWindow().save(QString("/tmp/omachat-permissions-bottom-%1-%2.png")
                    .arg(light ? "light" : "dark", compact ? "compact" : "wide")));
            QTest::keyClick(window, Qt::Key_Escape);
            ASSERT_TRUE(waitFor([&] { return !dialog->property("visible").toBool(); }));
            window->close();
            QTest::qWait(100);
        }
    }
}

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
