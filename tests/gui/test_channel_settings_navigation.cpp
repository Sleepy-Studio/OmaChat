#include "Harness.hpp"
#include "controllers/AppController.hpp"
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
class ChannelSettingsNavigation : public ::testing::TestWithParam<bool> {
protected:
    TestServer server;
    TestDaemon daemon{"settings-navigation"};
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
        ASSERT_TRUE(daemon.registerOn(server, "settings-navigation", "test-password"));
        ASSERT_TRUE(daemon.call("server.create", {{"name", "Settings navigation"}}).ok);
        qputenv("OMACHAT_SOCKET",
            QString("/tmp/omachat-test-settings-navigation-%1.sock").arg(QCoreApplication::applicationPid()).toUtf8());
        qputenv("XDG_CACHE_HOME", files.path().toUtf8());
        config::ClientConfig config;
        config.startup.launchDaemon = false;
        app = std::make_unique<client::AppController>(config);
        app->start();
        ASSERT_TRUE(waitFor([&] { return app->canSend(); }));
        id = app->selectedChannelId();
        theme.setReducedMotion(true);
        if (GetParam()) {
            QFile colors(files.filePath("light.toml"));
            ASSERT_TRUE(colors.open(QIODevice::WriteOnly));
            colors.write("background = '#fafafa'\nforeground = '#262626'\naccent = '#2563eb'\n");
            colors.close();
            ASSERT_TRUE(theme.loadFile(colors.fileName()));
        }
        QQmlComponent component(&engine);
        component.setData(R"QML(import QtQuick
import QtQuick.Controls
import OmaChat
ApplicationWindow { width:720; height:460; visible:true; color: Theme.background
    ChannelSettingsDialog { objectName:"editor" } }
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
    bool nativeFloated = false;
    void size(bool compact)
    {
        theme.setScale(compact ? 1.5 : 1.0);
        window->resize(compact ? 720 : 1440, compact ? 460 : 900);
        QTest::qWait(100);
        if (qEnvironmentVariableIsSet("OMACHAT_TEST_WAYLAND")) {
            const QString match = QString("pid:%1").arg(QCoreApplication::applicationPid());
            if (!nativeFloated) {
                ASSERT_EQ(
                    QProcess::execute("hyprctl",
                        {"eval", QString("hl.dispatch(hl.dsp.window.float({action='set',window='%1'}))").arg(match)}),
                    0);
                nativeFloated = true;
            }
            ASSERT_EQ(QProcess::execute("hyprctl",
                          {"eval",
                              QString("hl.dispatch(hl.dsp.window.resize({x=%1,y=%2,relative=false,window='%3'}))")
                                  .arg(compact ? 720 : 1440)
                                  .arg(compact ? 460 : 900)
                                  .arg(match)}),
                0);
            ASSERT_EQ(
                QProcess::execute("hyprctl", {"eval", QString("hl.dispatch(hl.dsp.focus({window='%1'}))").arg(match)}),
                0);
            QTest::qWait(100);
        }
        ASSERT_EQ(window->width(), compact ? 720 : 1440);
        ASSERT_EQ(window->height(), compact ? 460 : 900);
    }
    void bounds(QQuickItem* control)
    {
        ASSERT_TRUE(control);
        ASSERT_TRUE(control->isVisible());
        const QRectF rect = control->mapRectToScene(QRectF(0, 0, control->width(), control->height()));
        EXPECT_GT(rect.width(), 0);
        EXPECT_GT(rect.height(), 0);
        EXPECT_GE(rect.left(), 0);
        EXPECT_GE(rect.top(), 0);
        EXPECT_LE(rect.right(), window->width());
        EXPECT_LE(rect.bottom(), window->height());
    }
    void activate(QQuickItem* control)
    {
        ASSERT_TRUE(control);
        ASSERT_TRUE(control->isEnabled());
        control->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Space);
        QTest::qWait(50);
    }
};
TEST_P(ChannelSettingsNavigation, KeyboardSectionsKeepDraftAndCheckedSaveAndFixedActions)
{
    for (bool compact : {true, false}) {
        SCOPED_TRACE(compact ? "compact" : "wide");
        size(compact);
        ASSERT_TRUE(QMetaObject::invokeMethod(editor, "openFor", Q_ARG(QVariant, id)));
        QTest::qWait(100);
        auto* name = editor->findChild<QObject*>("channelName");
        ASSERT_TRUE(name);
        name->setProperty("text", compact ? "compact-draft" : "wide-draft");
        auto* overview = item("channelSettingsSection0");
        ASSERT_TRUE(overview);
        overview->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Right);
        QTest::qWait(50);
        EXPECT_EQ(editor->property("section").toInt(), 1);
        EXPECT_EQ(name->property("text").toString(), compact ? "compact-draft" : "wide-draft");
        bounds(item("channelSaveDetails"));
        bounds(item("channelCloseDetails"));
        bounds(item("channelChooseIcon"));
        activate(item("channelSettingsSection2"));
        EXPECT_EQ(editor->property("section").toInt(), 2);
        EXPECT_TRUE(editor->property("review").toMap().value("available").toBool());
        auto* scroll = item("channelSettingsScroll");
        ASSERT_TRUE(scroll);
        auto* view = scroll->property("contentItem").value<QQuickItem*>();
        ASSERT_TRUE(view);
        EXPECT_EQ(window->activeFocusItem(), view);
        QTest::keyClick(window, Qt::Key_End);
        if (compact) {
            EXPECT_GT(view->property("contentY").toReal(), 0);
        }
        EXPECT_NEAR(view->property("contentY").toReal(),
            std::max(0.0, view->property("contentHeight").toReal() - view->height()), 1.0);
        bounds(item("channelSaveDetails"));
        bounds(item("channelCloseDetails"));
        EXPECT_EQ(name->property("text").toString(), compact ? "compact-draft" : "wide-draft");
        EXPECT_TRUE(window->grabWindow().save(QString("/tmp/omachat-channel-settings-nav-%1-%2-permissions.png")
                .arg(GetParam() ? "light" : "dark", compact ? "compact" : "wide")));
        activate(item("channelSettingsSection1"));
        auto* bannerChoice = item("channelChooseBanner");
        ASSERT_TRUE(bannerChoice);
        bannerChoice->forceActiveFocus();
        QTest::qWait(50);
        auto* appearanceView = item("channelSettingsScroll")->property("contentItem").value<QQuickItem*>();
        ASSERT_TRUE(appearanceView);
        const auto bannerPosition = bannerChoice->mapToItem(appearanceView, QPointF(0, 0));
        EXPECT_GE(bannerPosition.y(), 0);
        EXPECT_LE(bannerPosition.y() + bannerChoice->height(), appearanceView->height());
        bounds(bannerChoice);
        EXPECT_TRUE(window->grabWindow().save(QString("/tmp/omachat-channel-settings-nav-%1-%2-appearance.png")
                .arg(GetParam() ? "light" : "dark", compact ? "compact" : "wide")));
        activate(item("channelSaveDetails"));
        ASSERT_TRUE(waitFor([&] { return !editor->property("saving").toBool(); }));
        EXPECT_FALSE(editor->property("dirty").toBool());
        EXPECT_FALSE(editor->property("saveFailed").toBool());
        EXPECT_EQ(app->channelDetails(id).value("name").toString(), compact ? "compact-draft" : "wide-draft");
        ASSERT_TRUE(QMetaObject::invokeMethod(editor, "close"));
        QTest::qWait(50);
    }
}
TEST_P(ChannelSettingsNavigation, OfflineRetainsDraftClearsReviewAndDisablesMutation)
{
    size(true);
    ASSERT_TRUE(QMetaObject::invokeMethod(editor, "openFor", Q_ARG(QVariant, id)));
    QTest::qWait(100);
    auto* name = editor->findChild<QObject*>("channelName");
    ASSERT_TRUE(name);
    name->setProperty("text", "offline retained");
    server.stop();
    ASSERT_TRUE(waitFor([&] { return app->state() != "connected"; }));
    EXPECT_TRUE(app->ready());
    EXPECT_TRUE(editor->property("visible").toBool());
    EXPECT_FALSE(editor->property("canManage").toBool());
    EXPECT_FALSE(item("channelSaveDetails")->isEnabled());
    activate(item("channelSettingsSection1"));
    EXPECT_FALSE(item("channelChooseIcon")->isEnabled());
    activate(item("channelSettingsSection2"));
    EXPECT_FALSE(editor->property("review").toMap().value("available").toBool());
    EXPECT_FALSE(item("channelSettingsEditOverrides")->isEnabled());
    EXPECT_EQ(name->property("text").toString(), "offline retained");
    activate(item("channelCloseDetails"));
    auto* confirm = editor->findChild<QObject*>("discardChannel");
    ASSERT_TRUE(confirm);
    EXPECT_TRUE(confirm->property("visible").toBool());
}
INSTANTIATE_TEST_SUITE_P(DarkAndLight, ChannelSettingsNavigation, ::testing::Bool());
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
