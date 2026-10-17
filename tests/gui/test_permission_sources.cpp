#include "Harness.hpp"
#include "controllers/AppController.hpp"
#include "platform/ThemeProvider.hpp"
#include "views/AudioLevels.hpp"
#include "views/VideoFrameItem.hpp"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
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
QVariantList propertyList(QObject* object, const char* name)
{
    const auto value = object->property(name);
    return value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant().toList()
                                                               : value.toList();
}
bool allowed(const QVariantMap& review, const QString& permission)
{
    for (const auto& row : review.value("rows").toList())
        if (row.toMap().value("name").toString() == permission)
            return row.toMap().value("allowed").toBool();
    return false;
}
struct PermissionSources : ::testing::Test {
    TestServer server;
    TestDaemon owner{"permission-owner"};
    TestDaemon reader{"permission-reader"};
    client::ThemeProvider theme{nullptr};
    std::unique_ptr<client::AppController> app;
    QString category, child, readerId;
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
            QString("/tmp/omachat-test-permission-owner-%1.sock").arg(QCoreApplication::applicationPid()).toUtf8());
        config::ClientConfig config;
        config.startup.launchDaemon = false;
        qputenv("XDG_CACHE_HOME", cache.path().toUtf8());
        app = std::make_unique<client::AppController>(config);
        app->start();
        ASSERT_TRUE(waitFor([&] { return app->channelPermissionReview(child).value("available").toBool(); }));
        app->selectChannel(child);
        ASSERT_TRUE(waitFor([&] { return app->serverMembers().size() == 2; }));
        for (const auto& member : app->serverMembers())
            if (member.toMap().value("userId").toString() != app->selfId())
                readerId = member.toMap().value("userId").toString();
        ASSERT_FALSE(readerId.isEmpty());
    }
    void TearDown() override
    {
        app.reset();
        qunsetenv("OMACHAT_SOCKET");
        qunsetenv("XDG_CACHE_HOME");
    }
};
} // namespace

TEST_F(PermissionSources, ServerConfirmedMemberPreviewClearsWhileLoadingAndOffline)
{
    app->loadPermissionPreview(child);
    ASSERT_TRUE(waitFor([&] { return app->permissionPreview().value("available").toBool(); }))
        << QJsonObject::fromVariantMap(app->permissionPreview()).toVariantMap().value("error").toString().toStdString()
        << QJsonDocument(QJsonObject::fromVariantMap(app->permissionPreview())).toJson().toStdString();
    auto preview = app->permissionPreview();
    EXPECT_TRUE(preview.value("supported").toBool());
    EXPECT_TRUE(preview.value("canPreviewMembers").toBool());
    EXPECT_TRUE(allowed(preview, "SEND_MESSAGES"));
    ASSERT_TRUE(owner
            .call("override.set",
                {{"channel", category}, {"user", "permission-reader"}, {"deny", QJsonArray{"SEND_MESSAGES"}}})
            .ok);
    app->loadPermissionPreview(child, readerId);
    EXPECT_TRUE(app->permissionPreview().value("rows").toList().isEmpty());
    ASSERT_TRUE(waitFor([&] {
        return app->permissionPreview().value("available").toBool()
            && app->permissionPreview().value("userId").toString() == readerId;
    }));
    EXPECT_FALSE(allowed(app->permissionPreview(), "SEND_MESSAGES"));
    for (const auto& row : app->permissionPreview().value("rows").toList())
        EXPECT_FALSE(row.toMap().value("source").toString().isEmpty());
    // Queue two subjects without pumping events; an older reply must not win.
    app->loadPermissionPreview(child, readerId);
    app->loadPermissionPreview(child);
    ASSERT_TRUE(waitFor([&] {
        return app->permissionPreview().value("available").toBool()
            && app->permissionPreview().value("userId").toString() == app->selfId();
    }));
    QTest::qWait(80);
    EXPECT_EQ(app->permissionPreview().value("userId").toString(), app->selfId());
    EXPECT_TRUE(allowed(app->permissionPreview(), "SEND_MESSAGES"));
    server.stop();
    ASSERT_TRUE(waitFor([&] { return app->state() != "connected"; }));
    EXPECT_TRUE(app->permissionPreview().value("rows").toList().isEmpty());
}

TEST_F(PermissionSources, ProductionDialogShowsMemberSourcesAndResetsOffline)
{
    ASSERT_TRUE(owner
            .call("override.set",
                {{"channel", category}, {"user", "permission-reader"}, {"deny", QJsonArray{"ATTACH_FILES"}}})
            .ok);
    theme.setScale(1.5);
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
    auto* window = qobject_cast<QQuickWindow*>(object.get());
    ASSERT_TRUE(dialog);
    ASSERT_TRUE(window);
    if (qEnvironmentVariableIsSet("OMACHAT_TEST_WAYLAND")) {
        QTest::qWait(150);
        const QString match = QString("pid:%1").arg(QCoreApplication::applicationPid());
        ASSERT_EQ(QProcess::execute("hyprctl",
                      {"eval", QString("hl.dispatch(hl.dsp.window.float({action='set',window='%1'}))").arg(match)}),
            0);
        ASSERT_EQ(
            QProcess::execute("hyprctl",
                {"eval",
                    QString("hl.dispatch(hl.dsp.window.resize({x=720,y=460,relative=false,window='%1'}))").arg(match)}),
            0);
        ASSERT_EQ(
            QProcess::execute("hyprctl", {"eval", QString("hl.dispatch(hl.dsp.focus({window='%1'}))").arg(match)}), 0);
    }
    ASSERT_TRUE(QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, child), Q_ARG(QVariant, "child")));
    ASSERT_TRUE(waitFor([&] {
        return namedControl(window->contentItem(), "permissionMemberPicker")
            && app->permissionPreview().value("available").toBool();
    }));
    auto* picker = namedControl(window->contentItem(), "permissionMemberPicker");
    ASSERT_TRUE(picker);
    EXPECT_TRUE(picker->isEnabled());
    // Select from the production picker's model rather than synthesizing review rows.
    const auto members = propertyList(dialog, "previewMembers");
    int memberIndex = -1;
    for (int i = 0; i < members.size(); ++i)
        if (members[i].toMap().value("userId").toString() == readerId)
            memberIndex = i;
    ASSERT_GE(memberIndex, 0);
    ASSERT_TRUE(QMetaObject::invokeMethod(picker, "activated", Q_ARG(int, memberIndex)));
    ASSERT_TRUE(waitFor([&] {
        auto* value = namedControl(window->contentItem(), "effectivePermission_ATTACH_FILES");
        return value && value->property("text").toString() == "Denied";
    }));
    EXPECT_EQ(dialog->property("previewUserId").toString(), readerId);
    auto* source = namedControl(window->contentItem(), "permissionSource_ATTACH_FILES");
    ASSERT_TRUE(source);
    EXPECT_FALSE(source->property("text").toString().isEmpty());
    EXPECT_EQ(source->property("textFormat").toInt(), int(Qt::PlainText));
    ASSERT_TRUE(
        owner.call("override.set", {{"channel", category}, {"user", "permission-reader"}, {"remove", true}}).ok);
    ASSERT_TRUE(waitFor([&] {
        auto* value = namedControl(window->contentItem(), "effectivePermission_ATTACH_FILES");
        return value && value->property("text").toString() == "Allowed";
    }));
    auto* scroll = object->findChild<QQuickItem*>("effectivePermissionReview");
    ASSERT_TRUE(scroll);
    scroll->forceActiveFocus();
    auto* view = scroll->property("contentItem").value<QQuickItem*>();
    ASSERT_TRUE(view);
    ASSERT_TRUE(waitFor([&] { return view->property("contentHeight").toDouble() > view->height(); }));
    QTest::keyClick(window, Qt::Key_End);
    ASSERT_TRUE(waitFor([&] { return view->property("contentY").toDouble() > 0; }));
    EXPECT_TRUE(window->grabWindow().save("/tmp/omachat-permission-sources-compact-dark.png"));
    QFile colors(cache.filePath("sources-light.toml"));
    ASSERT_TRUE(colors.open(QIODevice::WriteOnly));
    colors.write("background = '#fafafa'\nforeground = '#262626'\naccent = '#2563eb'\n");
    colors.close();
    ASSERT_TRUE(theme.loadFile(colors.fileName()));
    theme.changed();
    QTest::qWait(80);
    EXPECT_TRUE(window->grabWindow().save("/tmp/omachat-permission-sources-compact-light.png"));
    server.stop();
    ASSERT_TRUE(waitFor([&] { return propertyList(dialog, "accessRows").isEmpty(); }));
    EXPECT_TRUE(dialog->property("previewUserId").toString().isEmpty());
    EXPECT_EQ(namedControl(window->contentItem(), "effectivePermission_ATTACH_FILES"), nullptr);
    EXPECT_TRUE(object->findChild<QObject*>("permissionReviewSource")->property("text").toString().contains("Connect"));
}

TEST_F(PermissionSources, ProductionDialogClearsDeletedChannelAndCannotRepublishOldPreview)
{
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
    auto* window = qobject_cast<QQuickWindow*>(object.get());
    ASSERT_TRUE(dialog);
    ASSERT_TRUE(window);
    ASSERT_TRUE(QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, child), Q_ARG(QVariant, "child")));
    ASSERT_TRUE(waitFor([&] { return !propertyList(dialog, "accessRows").isEmpty(); }));
    dialog->setProperty("previewUserId", readerId);
    ASSERT_TRUE(owner.call("channel.delete", {{"channel", child}}).ok);
    ASSERT_TRUE(waitFor([&] { return !app->channelPermissionReview(child).value("available").toBool(); }));
    ASSERT_TRUE(waitFor([&] { return propertyList(dialog, "accessRows").isEmpty(); }));
    EXPECT_TRUE(dialog->property("visible").toBool());
    // Pump remaining queued replies, then retry the deleted context explicitly.
    QTest::qWait(150);
    EXPECT_TRUE(app->permissionPreview().value("rows").toList().isEmpty());
    app->loadPermissionPreview(child, readerId);
    QTest::qWait(80);
    EXPECT_FALSE(app->permissionPreview().value("available").toBool());
    EXPECT_TRUE(propertyList(dialog, "accessRows").isEmpty());
    EXPECT_EQ(namedControl(window->contentItem(), "effectivePermission_SEND_MESSAGES"), nullptr);
}

TEST_F(PermissionSources, PendingMemberPreviewDoesNotSurviveLeavingServerContext)
{
    app->loadPermissionPreview(child, readerId);
    app->selectHome();
    EXPECT_TRUE(app->permissionPreview().value("rows").toList().isEmpty());
    QTest::qWait(150);
    EXPECT_FALSE(app->permissionPreview().value("available").toBool());
    EXPECT_TRUE(app->permissionPreview().value("rows").toList().isEmpty());
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
