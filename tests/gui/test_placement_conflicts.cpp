#include "Harness.hpp"
#include "controllers/AppController.hpp"
#include "platform/ThemeProvider.hpp"
#include "views/AudioLevels.hpp"
#include "views/VideoFrameItem.hpp"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJSValue>
#include <QJsonArray>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTest>
#include <QThreadPool>
#include <gtest/gtest.h>

using namespace omachat;
using namespace omachat::test;

namespace {
class PlacementConflicts : public ::testing::Test {
protected:
    TestServer server;
    TestDaemon daemon{"placement-conflicts"};
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
        ASSERT_TRUE(daemon.registerOn(server, "placement-conflicts", "test-password"));
        ASSERT_TRUE(daemon.call("server.create", {{"name", "Placement conflicts"}}).ok);
        qputenv("OMACHAT_SOCKET",
            QString("/tmp/omachat-test-placement-conflicts-%1.sock").arg(QCoreApplication::applicationPid()).toUtf8());
        qputenv("XDG_CACHE_HOME", files.path().toUtf8());
        config::ClientConfig config;
        config.startup.launchDaemon = false;
        app = std::make_unique<client::AppController>(config);
        app->start();
        ASSERT_TRUE(waitFor([&] { return app->canSend(); }));
        id = app->selectedChannelId();
        theme.setReducedMotion(true);
        if (qEnvironmentVariableIsSet("OMACHAT_CAPTURE_DIR")) {
            bool validScale = false;
            const auto captureScale = qEnvironmentVariable("OMACHAT_CAPTURE_SCALE").toDouble(&validScale);
            theme.setScale(validScale ? captureScale : 1.5);
            if (qEnvironmentVariable("OMACHAT_CAPTURE_THEME") == "light") {
                QFile palette(files.filePath("capture-light.toml"));
                ASSERT_TRUE(palette.open(QIODevice::WriteOnly));
                palette.write("background = '#fafafa'\nforeground = '#262626'\naccent = '#2563eb'\n");
                palette.close();
                ASSERT_TRUE(theme.loadFile(palette.fileName()));
            }
        }
        QQmlComponent component(&engine);
        component.setData(R"QML(import QtQuick
import QtQuick.Controls
import OmaChat
ApplicationWindow { width:720; height:460; visible:true; color: Theme.background
    ChannelPlacementDialog { objectName:"editor" } }
)QML",
            QUrl());
        object.reset(component.create());
        ASSERT_TRUE(object) << component.errorString().toStdString();
        window = qobject_cast<QQuickWindow*>(object.get());
        ASSERT_TRUE(window);
        if (qEnvironmentVariableIsSet("OMACHAT_TEST_WAYLAND")) {
            ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
            const QString match = QStringLiteral("pid:%1").arg(QCoreApplication::applicationPid());
            ASSERT_EQ(QProcess::execute("hyprctl",
                          {"eval", QString("hl.dispatch(hl.dsp.window.float({action='set',window='%1'}))").arg(match)}),
                0);
            ASSERT_EQ(QProcess::execute("hyprctl",
                          {"eval",
                              QString("hl.dispatch(hl.dsp.window.resize({x=720,y=460,relative=false,window='%1'}))")
                                  .arg(match)}),
                0);
            ASSERT_EQ(
                QProcess::execute("hyprctl", {"eval", QString("hl.dispatch(hl.dsp.focus({window='%1'}))").arg(match)}),
                0);
            ASSERT_TRUE(waitFor([&] { return window->width() == 720 && window->height() == 460; }));
        }
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
    void capture(const QString& state, const QStringList& fixedControls)
    {
        const auto root = qEnvironmentVariable("OMACHAT_CAPTURE_DIR");
        if (root.isEmpty())
            return;
        ASSERT_TRUE(QDir().mkpath(root));
        QTest::qWait(100);
        for (const auto& name : fixedControls) {
            const auto* control = item(name);
            ASSERT_TRUE(control) << name.toStdString();
            ASSERT_TRUE(control->isVisible()) << name.toStdString();
            const auto bounds = control->mapRectToScene(QRectF(0, 0, control->width(), control->height()));
            EXPECT_GE(bounds.left(), 0);
            EXPECT_GE(bounds.top(), 0);
            EXPECT_LE(bounds.right(), window->width());
            EXPECT_LE(bounds.bottom(), window->height());
        }
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        const QString name = QString("%1-%2-%3-%4-%5.png")
                                 .arg(info->test_suite_name(), info->name(), state,
                                     qEnvironmentVariable("OMACHAT_CAPTURE_THEME", "dark"),
                                     qEnvironmentVariable("OMACHAT_CAPTURE_SCALE", "1.5"));
        ASSERT_TRUE(window->grabWindow().save(QDir(root).filePath(name)));
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
    QString create(const QString& name, const QString& type, const QString& parent = "0")
    {
        const auto reply = daemon.call("channel.create",
            {{"server", app->selectedServerId()}, {"name", name}, {"type", type}, {"parent", parent}});
        if (!reply.ok)
            return {};
        const QString created = reply.result.value("id").toString();
        if (!waitFor([&] {
                return !app->channelDetails(created).isEmpty()
                    && app->channelDetails(id).value("placement_token").toString()
                    == reply.result.value("placement_token").toString();
            }))
            return {};
        return created;
    }
    void chooseCategory(const QString& category)
    {
        auto* picker = item("placementCategory");
        ASSERT_TRUE(picker);
        const auto choices = editor->property("categories").toList();
        int selected = -1;
        for (int i = 0; i < choices.size(); ++i)
            if (choices.at(i).toMap().value("id").toString() == category)
                selected = i;
        ASSERT_GE(selected, 0);
        picker->setProperty("currentIndex", selected);
        ASSERT_TRUE(QMetaObject::invokeMethod(picker, "activated", Q_ARG(int, selected)));
    }
    QVariantList listProperty(const char* name) const
    {
        const auto value = editor->property(name);
        return value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant().toList()
                                                                   : value.toList();
    }
    void reload()
    {
        QSignalSpy completed(app.get(), &client::AppController::administrationFinished);
        ASSERT_TRUE(QMetaObject::invokeMethod(editor, "reloadPlacement"));
        ASSERT_TRUE(waitFor([&] {
            for (const auto& result : completed)
                if (result.at(0).toString() == "channel.placement.reload" && result.at(1).toString() == id)
                    return !editor->property("saving").toBool();
            return false;
        }));
    }
};

TEST_F(PlacementConflicts, RemoteSourceMoveBlocksSaveAndExplicitReloadRetainsDraft)
{
    const auto category = create("destination", "category");
    ASSERT_FALSE(category.isEmpty());
    const auto sibling = create("sibling", "text", category);
    ASSERT_FALSE(sibling.isEmpty());
    open();
    chooseCategory(category);
    item("placementPosition")->setProperty("currentIndex", 0);
    EXPECT_EQ(item("placementPosition")->property("currentValue").toString(), sibling);
    ASSERT_TRUE(daemon.call("channel.update", {{"channel", id}, {"parent", category}, {"before", "0"}}).ok);
    ASSERT_TRUE(waitFor([&] { return editor->property("needsReload").toBool(); }));
    EXPECT_TRUE(editor->property("visible").toBool());
    EXPECT_FALSE(item("placementSave")->isEnabled());
    EXPECT_TRUE(item("placementReload")->isVisible());
    capture("conflict", {"placementSave", "placementReload"});
    EXPECT_EQ(item("placementCategory")->property("currentValue").toString(), category);
    EXPECT_EQ(item("placementPosition")->property("currentValue").toString(), sibling);
    EXPECT_TRUE(item("placementStatus")->property("text").toString().contains("retained"));
    reload();
    EXPECT_FALSE(editor->property("needsReload").toBool());
    EXPECT_TRUE(item("placementSave")->isEnabled());
    capture("reloaded", {"placementSave"});
    EXPECT_EQ(item("placementCategory")->property("currentValue").toString(), category);
    EXPECT_EQ(item("placementPosition")->property("currentValue").toString(), sibling);
    item("placementSave")->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_Space);
    ASSERT_TRUE(waitFor([&] { return !editor->property("saving").toBool(); }));
    EXPECT_FALSE(editor->property("failed").toBool());
    EXPECT_FALSE(editor->property("needsReload").toBool());
}

TEST_F(PlacementConflicts, DeletedBeforeSiblingRequiresReviewAndReloadExplainsFallback)
{
    const auto category = create("destination", "category");
    ASSERT_FALSE(category.isEmpty());
    const auto sibling = create("sibling", "text", category);
    ASSERT_FALSE(sibling.isEmpty());
    open();
    chooseCategory(category);
    item("placementPosition")->setProperty("currentIndex", 0);
    ASSERT_TRUE(daemon.call("channel.delete", {{"channel", sibling}}).ok);
    ASSERT_TRUE(waitFor([&] { return editor->property("needsReload").toBool(); }));
    EXPECT_EQ(item("placementPosition")->property("currentValue").toString(), sibling);
    EXPECT_FALSE(item("placementSave")->isEnabled());
    reload();
    EXPECT_EQ(item("placementCategory")->property("currentValue").toString(), category);
    EXPECT_EQ(item("placementPosition")->property("currentValue").toString(), "");
    EXPECT_TRUE(item("placementStatus")->property("text").toString().contains("no longer available"));
    EXPECT_FALSE(editor->property("needsReload").toBool());
}

TEST_F(PlacementConflicts, OfflineRetainsChoicesAndDisablesMutationEvenWhenReady)
{
    open();
    const auto positions = listProperty("positions");
    server.stop();
    ASSERT_TRUE(waitFor([&] { return app->state() != "connected"; }));
    EXPECT_TRUE(app->ready());
    EXPECT_TRUE(editor->property("visible").toBool());
    EXPECT_EQ(listProperty("positions"), positions);
    EXPECT_FALSE(item("placementSave")->isEnabled());
    EXPECT_FALSE(item("placementCategory")->isEnabled());
    EXPECT_FALSE(item("placementPosition")->isEnabled());
    EXPECT_TRUE(item("placementStatus")->property("text").toString().contains("Reconnect"));
}

TEST_F(PlacementConflicts, ConflictReplyPreservesChoicesAndPendingOperationBlocksReload)
{
    const auto sibling = create("chosen-sibling", "text", app->channelDetails(id).value("parent_id").toString());
    ASSERT_FALSE(sibling.isEmpty());
    open();
    const auto available = listProperty("positions");
    int chosenIndex = -1;
    for (int index = 0; index < available.size(); ++index)
        if (available.at(index).toMap().value("id").toString() == sibling)
            chosenIndex = index;
    ASSERT_GE(chosenIndex, 0);
    item("placementPosition")->setProperty("currentIndex", chosenIndex);
    ASSERT_TRUE(waitFor([&] { return item("placementPosition")->property("currentValue").toString() == sibling; }));
    const auto categoryId = item("placementCategory")->property("currentValue").toString();
    const auto beforeId = item("placementPosition")->property("currentValue").toString();
    ASSERT_EQ(beforeId, sibling);
    const auto positions = listProperty("positions");
    editor->setProperty("needsReload", true);
    editor->setProperty("saving", true);
    EXPECT_FALSE(item("placementReload")->isEnabled());
    EXPECT_FALSE(item("placementSave")->isEnabled());
    EXPECT_FALSE(item("placementPosition")->isEnabled());
    emit app->administrationFinished("channel.move", id, "Placement changed. Reload and review.", {{"conflict", true}});
    EXPECT_FALSE(editor->property("saving").toBool());
    EXPECT_TRUE(editor->property("needsReload").toBool());
    EXPECT_EQ(listProperty("positions"), positions);
    EXPECT_EQ(item("placementCategory")->property("currentValue").toString(), categoryId);
    EXPECT_EQ(item("placementPosition")->property("currentValue").toString(), beforeId);
    EXPECT_FALSE(item("placementSave")->isEnabled());
    EXPECT_TRUE(item("placementReload")->isEnabled());
    reload();
    EXPECT_FALSE(editor->property("needsReload").toBool());
    EXPECT_TRUE(item("placementSave")->isEnabled());
    EXPECT_EQ(item("placementCategory")->property("currentValue").toString(), categoryId);
    EXPECT_EQ(item("placementPosition")->property("currentValue").toString(), beforeId);
}

TEST_F(PlacementConflicts, WholeListTokenIsCapturedAndOnlyExplicitReloadAdvancesIt)
{
    open();
    auto original = editor->property("originalPlacement").value<QJSValue>().toVariant().toMap();
    const auto token = original.value("placement_token").toString();
    ASSERT_FALSE(token.isEmpty());
    const auto category = create("another-category", "category");
    ASSERT_FALSE(category.isEmpty());
    ASSERT_TRUE(waitFor([&] { return editor->property("needsReload").toBool(); }));
    original = editor->property("originalPlacement").value<QJSValue>().toVariant().toMap();
    EXPECT_EQ(original.value("placement_token").toString(), token);
    reload();
    original = editor->property("originalPlacement").value<QJSValue>().toVariant().toMap();
    EXPECT_NE(original.value("placement_token").toString(), token);
    EXPECT_FALSE(editor->property("needsReload").toBool());
}

TEST_F(PlacementConflicts, CosmeticChannelRenameDoesNotInvalidatePlacement)
{
    const auto sibling = create("sibling", "text");
    ASSERT_FALSE(sibling.isEmpty());
    open();
    ASSERT_TRUE(daemon.call("channel.update", {{"channel", sibling}, {"name", "renamed"}}).ok);
    ASSERT_TRUE(waitFor([&] { return app->channelDetails(sibling).value("name").toString() == "renamed"; }));
    EXPECT_FALSE(editor->property("needsReload").toBool());
    EXPECT_TRUE(item("placementSave")->isEnabled());
}
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
