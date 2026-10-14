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
struct PermissionTargets : ::testing::Test {
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
        theme.setScale(captureWide() ? 1.0 : 1.5);
        if (captureLight()) {
            QFile colors(cache.filePath("targets-light.toml"));
            ASSERT_TRUE(colors.open(QIODevice::WriteOnly));
            colors.write("background = '#fafafa'\nforeground = '#262626'\naccent = '#2563eb'\n");
            colors.close();
            ASSERT_TRUE(theme.loadFile(colors.fileName()));
            theme.changed();
            EXPECT_EQ(theme.background(), QColor("#fafafa"));
            EXPECT_EQ(theme.text(), QColor("#262626"));
        }
        EXPECT_EQ(theme.dark(), !captureLight());
        EXPECT_DOUBLE_EQ(theme.scale(), captureWide() ? 1.0 : 1.5);
    }
    bool captureWide() const { return qEnvironmentVariable("OMACHAT_CAPTURE_SIZE") == "wide"; }
    bool captureLight() const { return qEnvironmentVariable("OMACHAT_CAPTURE_THEME") == "light"; }
    void configureCaptureWindow(QQuickWindow* window)
    {
        const int width = captureWide() ? 1440 : 720;
        const int height = captureWide() ? 900 : 460;
        window->setWidth(width);
        window->setHeight(height);
        if (qEnvironmentVariableIsSet("OMACHAT_TEST_WAYLAND")) {
            ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
            QTest::qWait(150);
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
            ASSERT_TRUE(waitFor([&] {
                return qAbs(window->width() - width) <= 1 && qAbs(window->height() - height) <= 1;
            })) << "actual="
                << window->width() << 'x' << window->height();
        }
        window->requestActivate();
        ASSERT_TRUE(waitFor([&] { return QGuiApplication::focusWindow() == window; }));
        EXPECT_LE(qAbs(window->width() - width), 1);
        EXPECT_LE(qAbs(window->height() - height), 1);
    }
    void TearDown() override
    {
        app.reset();
        qunsetenv("OMACHAT_SOCKET");
        qunsetenv("XDG_CACHE_HOME");
    }
};
} // namespace

TEST_F(PermissionTargets, RoleOnlyAndCreationSeparateRealMemberOverridesAndIgnoreStaleReplies)
{
    const auto role = owner.call("role.create",
        {{"server", app->selectedServerId()}, {"name", "<b>Contributing role</b>"},
            {"permissions", QJsonArray{"SEND_MESSAGES"}}});
    ASSERT_TRUE(role.ok) << role.errorMessage.toStdString();
    const QString roleId = role.result.value("id").toString();
    ASSERT_TRUE(
        owner.call("override.set", {{"channel", category}, {"role", roleId}, {"deny", QJsonArray{"SEND_MESSAGES"}}})
            .ok);
    ASSERT_TRUE(owner
            .call("override.set",
                {{"channel", category}, {"user", "permission-reader"}, {"allow", QJsonArray{"SEND_MESSAGES"}}})
            .ok);
    app->loadPermissionPreview(child, {}, roleId);
    ASSERT_TRUE(waitFor([&] { return app->permissionPreview().value("available").toBool(); }));
    EXPECT_EQ(app->permissionPreview().value("roleId").toString(), roleId);
    EXPECT_EQ(app->permissionPreview().value("userId").toString(), "0");
    EXPECT_FALSE(allowed(app->permissionPreview(), "SEND_MESSAGES"));
    bool named = false;
    for (const auto& row : app->permissionPreview().value("rows").toList())
        if (row.toMap().value("name").toString() == "SEND_MESSAGES")
            named = row.toMap().value("source").toString().contains("<b>Contributing role</b>");
    EXPECT_TRUE(named);
    app->loadCreationPermissionPreview("text", category, {}, roleId);
    EXPECT_TRUE(app->permissionPreview().value("rows").toList().isEmpty());
    ASSERT_TRUE(waitFor([&] { return app->permissionPreview().value("available").toBool(); }));
    EXPECT_FALSE(allowed(app->permissionPreview(), "SEND_MESSAGES"));
    app->loadCreationPermissionPreview("text", category, readerId);
    ASSERT_TRUE(waitFor([&] { return app->permissionPreview().value("available").toBool(); }));
    EXPECT_TRUE(allowed(app->permissionPreview(), "SEND_MESSAGES"));
    app->loadCreationPermissionPreview("text", category, {}, roleId);
    app->loadCreationPermissionPreview("voice", "0");
    ASSERT_TRUE(waitFor([&] { return app->permissionPreview().value("available").toBool(); }));
    QTest::qWait(80);
    EXPECT_EQ(app->permissionPreview().value("channelType").toString(), "voice");
    EXPECT_EQ(app->permissionPreview().value("parentId").toString(), "0");
    EXPECT_TRUE(app->permissionPreview().value("roleId").toString().isEmpty());
    app->loadCreationPermissionPreview("text", category, readerId);
    app->selectHome();
    QTest::qWait(100);
    EXPECT_TRUE(app->permissionPreview().value("rows").toList().isEmpty());
}

TEST_F(PermissionTargets, ProductionCreationRetainsDraftAndShowsPlainRoleSourcesWithKeyboardScroll)
{
    const auto role = owner.call("role.create",
        {{"server", app->selectedServerId()}, {"name", "<b>Contributing role</b>"},
            {"permissions", QJsonArray{"SEND_MESSAGES"}}});
    ASSERT_TRUE(role.ok);
    const QString roleId = role.result.value("id").toString();
    ASSERT_TRUE(
        owner.call("override.set", {{"channel", category}, {"role", roleId}, {"deny", QJsonArray{"SEND_MESSAGES"}}})
            .ok);
    ASSERT_TRUE(waitFor([&] { return app->serverRoles().size() >= 2; }));
    const bool wide = captureWide();
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls
import OmaChat
ApplicationWindow {
    width: 720; height: 460; visible: true
    CreateChannelDialog { objectName: "creation" }
})",
        QUrl());
    std::unique_ptr<QObject> object(component.create());
    ASSERT_TRUE(object) << component.errorString().toStdString();
    auto* window = qobject_cast<QQuickWindow*>(object.get());
    auto* dialog = object->findChild<QObject*>("creation");
    ASSERT_TRUE(window);
    ASSERT_TRUE(dialog);
    ASSERT_NO_FATAL_FAILURE(configureCaptureWindow(window));
    dialog->setProperty("parentId", category);
    ASSERT_TRUE(QMetaObject::invokeMethod(dialog, "open"));
    ASSERT_TRUE(waitFor([&] { return dialog->property("visible").toBool(); }));
    auto* name = object->findChild<QObject*>("channelName");
    ASSERT_TRUE(name);
    name->setProperty("text", "Retained draft");
    dialog->setProperty("reviewingAccess", true);
    dialog->setProperty("previewRoleId", roleId);
    ASSERT_TRUE(waitFor([&] { return app->permissionPreview().value("available").toBool(); }));
    EXPECT_EQ(name->property("text").toString(), "Retained draft");
    auto* source = namedControl(window->contentItem(), "creationPermissionSource_SEND_MESSAGES");
    ASSERT_TRUE(source);
    EXPECT_EQ(source->property("textFormat").toInt(), int(Qt::PlainText));
    EXPECT_TRUE(source->property("text").toString().contains("<b>Contributing role</b>"));
    EXPECT_TRUE(dialog->property("accessSource").toString().contains("no real member"));
    auto* scroll = object->findChild<QQuickItem*>("creationFormScroll");
    ASSERT_TRUE(scroll);
    window->requestActivate();
    ASSERT_TRUE(waitFor([&] { return QGuiApplication::focusWindow() == window; }));
    // Opening the popup finishes by focusing Name. Wait for that transition
    // and the source-row layout before deliberately moving keyboard focus.
    ASSERT_TRUE(waitFor([&] { return dialog->property("opened").toBool(); }));
    auto* view = scroll->property("contentItem").value<QQuickItem*>();
    ASSERT_TRUE(view);
    if (!wide) {
        ASSERT_TRUE(waitFor([&] { return view->property("contentHeight").toDouble() > view->height(); }));
    }
    // Follow the production review action: it moves focus to the viewport.
    auto* reviewButton = namedControl(window->contentItem(), "reviewCreationAccess");
    ASSERT_TRUE(reviewButton);
    reviewButton->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_Space);
    ASSERT_FALSE(dialog->property("reviewingAccess").toBool());
    QTest::keyClick(window, Qt::Key_Space);
    ASSERT_TRUE(
        waitFor([&] { return dialog->property("reviewingAccess").toBool() && window->activeFocusItem() == view; }));
    ASSERT_TRUE(waitFor([&] {
        return app->permissionPreview().value("available").toBool()
            && !app->permissionPreview().value("loading").toBool()
            && app->permissionPreview().value("roleId").toString() == roleId
            && !propertyList(dialog, "accessRows").isEmpty();
    }));
    QTest::qWait(50);
    QTest::keyClick(window, Qt::Key_End);
    if (view->property("contentHeight").toDouble() > view->height()) {
        ASSERT_TRUE(waitFor([&] { return view->property("contentY").toDouble() > 0; }))
            << "contentHeight=" << view->property("contentHeight").toDouble() << " viewport=" << view->height()
            << " scrollFocus=" << scroll->hasActiveFocus() << " activeFocusItem="
            << (window->activeFocusItem() ? window->activeFocusItem()->objectName().toStdString() : "none");
    }
    auto* create = namedControl(window->contentItem(), "channelCreate");
    ASSERT_TRUE(create);
    const QPointF pos = create->mapToScene(QPointF(0, 0));
    EXPECT_GE(pos.x(), 0);
    EXPECT_GE(pos.y(), 0);
    EXPECT_LE(pos.x() + create->width(), window->width());
    EXPECT_LE(pos.y() + create->height(), window->height());
    const QString captureDir = qEnvironmentVariable("OMACHAT_CAPTURE_DIR");
    if (!captureDir.isEmpty()) {
        EXPECT_TRUE(window->grabWindow().save(captureDir + "/creation-role-preview.png"));
        view->setProperty("contentY", 0);
        QTest::qWait(60);
        EXPECT_TRUE(window->grabWindow().save(captureDir + "/creation-role-preview-top.png"));
    }
    server.stop();
    ASSERT_TRUE(waitFor([&] { return propertyList(dialog, "accessRows").isEmpty(); }));
    EXPECT_EQ(name->property("text").toString(), "Retained draft");
}

TEST_F(PermissionTargets, ProductionExistingChannelPickerUsesHypotheticalRoleAndPlainNamedSource)
{
    const auto role = owner.call("role.create",
        {{"server", app->selectedServerId()}, {"name", "<b>Existing role</b>"},
            {"permissions", QJsonArray{"SEND_MESSAGES"}}});
    ASSERT_TRUE(role.ok);
    const QString roleId = role.result.value("id").toString();
    ASSERT_TRUE(
        owner.call("override.set", {{"channel", child}, {"role", roleId}, {"deny", QJsonArray{"SEND_MESSAGES"}}}).ok);
    ASSERT_TRUE(waitFor([&] { return app->serverRoles().size() >= 2; }));
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
    ASSERT_NO_FATAL_FAILURE(configureCaptureWindow(window));
    ASSERT_TRUE(QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, child), Q_ARG(QVariant, "child")));
    ASSERT_TRUE(waitFor([&] { return app->permissionPreview().value("available").toBool(); }));
    auto* picker = namedControl(window->contentItem(), "permissionMemberPicker");
    ASSERT_TRUE(picker);
    const auto subjects = propertyList(dialog, "previewMembers");
    int index = -1;
    for (int i = 0; i < subjects.size(); ++i)
        if (subjects[i].toMap().value("roleId").toString() == roleId)
            index = i;
    ASSERT_GE(index, 0);
    ASSERT_TRUE(QMetaObject::invokeMethod(picker, "activated", Q_ARG(int, index)));
    ASSERT_TRUE(waitFor([&] {
        return app->permissionPreview().value("available").toBool()
            && app->permissionPreview().value("roleId").toString() == roleId;
    }));
    EXPECT_TRUE(dialog->property("accessSource").toString().contains("no real member"));
    auto* source = namedControl(window->contentItem(), "permissionSource_SEND_MESSAGES");
    ASSERT_TRUE(source);
    EXPECT_EQ(source->property("textFormat").toInt(), int(Qt::PlainText));
    EXPECT_TRUE(source->property("text").toString().contains("<b>Existing role</b>"));
    EXPECT_FALSE(allowed(app->permissionPreview(), "SEND_MESSAGES"));
    ASSERT_TRUE(waitFor([&] { return dialog->property("opened").toBool(); }));
    QQuickItem* close = nullptr;
    for (auto* control : object->findChildren<QQuickItem*>())
        if (control->isVisible() && control->property("tip").toString() == "Close") {
            close = control;
            break;
        }
    ASSERT_TRUE(close);
    const QPointF closePosition = close->mapToScene(QPointF(0, 0));
    EXPECT_GE(closePosition.x(), 0);
    EXPECT_GE(closePosition.y(), 0);
    EXPECT_LE(closePosition.x() + close->width(), window->width());
    EXPECT_LE(closePosition.y() + close->height(), window->height());
    auto* scroll = object->findChild<QQuickItem*>("effectivePermissionReview");
    ASSERT_TRUE(scroll);
    auto* viewport = scroll->property("contentItem").value<QQuickItem*>();
    ASSERT_TRUE(viewport);
    if (!captureWide()) {
        ASSERT_TRUE(waitFor([&] { return viewport->property("contentHeight").toDouble() > viewport->height(); }));
    }
    scroll->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_End);
    if (viewport->property("contentHeight").toDouble() > viewport->height()) {
        ASSERT_TRUE(waitFor([&] { return viewport->property("contentY").toDouble() > 0; }));
    }
    const QString captureDir = qEnvironmentVariable("OMACHAT_CAPTURE_DIR");
    if (!captureDir.isEmpty()) {
        EXPECT_TRUE(window->grabWindow().save(captureDir + "/existing-role-preview.png"));
        viewport->setProperty("contentY", 0);
        QTest::qWait(60);
        EXPECT_TRUE(window->grabWindow().save(captureDir + "/existing-role-preview-top.png"));
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
