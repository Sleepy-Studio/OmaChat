#include "Harness.hpp"
#include "NativeWindowHarness.hpp"
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
class ArtworkConflicts : public ::testing::Test {
protected:
    TestServer server;
    TestDaemon daemon{"artwork-conflicts"};
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
        ASSERT_TRUE(daemon.registerOn(server, "artwork-conflicts", "test-password"));
        ASSERT_TRUE(daemon.call("server.create", {{"name", "Artwork conflicts"}}).ok);
        qputenv("OMACHAT_SOCKET",
            QString("/tmp/omachat-test-artwork-conflicts-%1.sock").arg(QCoreApplication::applicationPid()).toUtf8());
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
    ArtworkCropDialog { objectName:"editor" }
    ChannelSettingsDialog { objectName:"settingsEditor" } }
)QML",
            QUrl());
        object.reset(component.create());
        ASSERT_TRUE(object) << component.errorString().toStdString();
        window = qobject_cast<QQuickWindow*>(object.get());
        ASSERT_TRUE(window);
        ASSERT_TRUE(prepareNativeWindow(window, 720, 460));
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
            for (auto* ancestor = control->parentItem(); ancestor; ancestor = ancestor->parentItem()) {
                if (!ancestor->clip())
                    continue;
                const auto viewport = ancestor->mapRectToScene(QRectF(0, 0, ancestor->width(), ancestor->height()));
                EXPECT_TRUE(viewport.adjusted(-1, -1, 1, 1).contains(bounds)) << name.toStdString();
            }
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
    QUrl image()
    {
        const QString path = files.filePath("crop.png");
        QImage source(160, 80, QImage::Format_RGB32);
        source.fill(Qt::blue);
        if (!source.save(path))
            return {};
        return QUrl::fromLocalFile(path);
    }
    void open()
    {
        const auto url = image();
        ASSERT_FALSE(url.isEmpty());
        ASSERT_TRUE(QMetaObject::invokeMethod(
            editor, "openFor", Q_ARG(QVariant, id), Q_ARG(QVariant, "icon"), Q_ARG(QVariant, url)));
        ASSERT_TRUE(waitFor([&] { return !editor->property("fingerprint").toString().isEmpty(); }));
        ASSERT_TRUE(item("artworkCropApply"));
    }
};

TEST_F(ArtworkConflicts, RealCheckedCropConflictRetainsCropAndExplicitReloadEnablesSave)
{
    open();
    editor->setProperty("focalX", 0.25);
    editor->setProperty("focalY", 0.75);
    editor->setProperty("zoom", 2.0);
    const auto preview = editor->property("preview");
    const auto fingerprint = editor->property("fingerprint").toString();
    const auto url = editor->property("fileUrl").toUrl();
    // A stale baseline not represented in the visible model exercises the
    // daemon/server compare-and-set path rather than just the UI preflight.
    const QVariantMap stale{{"icon_attachment_id", "99999999"}, {"banner_attachment_id", "0"}};
    editor->setProperty("originalArtwork", stale);
    editor->setProperty("uploading", true);
    app->setChannelArtworkCrop(id, "icon", url, 0.25, 0.75, 2.0, fingerprint, stale);
    ASSERT_TRUE(waitFor([&] { return !editor->property("uploading").toBool(); }));
    EXPECT_TRUE(editor->property("visible").toBool());
    EXPECT_TRUE(editor->property("needsReload").toBool());
    EXPECT_FALSE(editor->property("error").toString().isEmpty());
    EXPECT_EQ(editor->property("preview"), preview);
    EXPECT_EQ(editor->property("fingerprint").toString(), fingerprint);
    EXPECT_DOUBLE_EQ(editor->property("focalX").toDouble(), 0.25);
    EXPECT_DOUBLE_EQ(editor->property("focalY").toDouble(), 0.75);
    EXPECT_DOUBLE_EQ(editor->property("zoom").toDouble(), 2.0);
    EXPECT_FALSE(item("artworkCropApply")->isEnabled());
    EXPECT_TRUE(item("artworkCropReload")->isVisible());
    capture("conflict", {"artworkCropApply", "artworkCropReload"});
    ASSERT_TRUE(QMetaObject::invokeMethod(editor, "reloadArtwork"));
    EXPECT_TRUE(editor->property("reloading").toBool());
    EXPECT_FALSE(item("artworkCropApply")->isEnabled());
    ASSERT_TRUE(waitFor([&] { return !editor->property("reloading").toBool(); }));
    EXPECT_FALSE(editor->property("needsReload").toBool());
    EXPECT_EQ(editor->property("fileUrl").toUrl(), url);
    EXPECT_EQ(editor->property("preview"), preview);
    EXPECT_DOUBLE_EQ(editor->property("zoom").toDouble(), 2.0);
    EXPECT_TRUE(item("artworkCropApply")->isEnabled());
    capture("reloaded", {"artworkCropApply"});
    item("artworkCropApply")->forceActiveFocus();
    ASSERT_TRUE(activateNativeWindow(window));
    QTest::keyClick(window, Qt::Key_Space);
    ASSERT_TRUE(waitFor([&] { return !editor->property("visible").toBool(); }));
    ASSERT_TRUE(waitFor([&] { return app->channelDetails(id).value("icon_attachment_id").toString() != "0"; }));
}

TEST_F(ArtworkConflicts, VisibleArtworkAdditionPreservesCropUntilExplicitReload)
{
    open();
    editor->setProperty("zoom", 1.75);
    const auto preview = editor->property("preview");
    const auto generation = app->artworkGeneration();
    ASSERT_TRUE(
        daemon.call("channel.artwork.set", {{"channel", id}, {"kind", "banner"}, {"file", image().toLocalFile()}}).ok);
    ASSERT_TRUE(waitFor([&] { return editor->property("needsReload").toBool(); }));
    EXPECT_EQ(app->artworkGeneration(), generation);
    EXPECT_TRUE(editor->property("visible").toBool());
    EXPECT_EQ(editor->property("preview"), preview);
    EXPECT_DOUBLE_EQ(editor->property("zoom").toDouble(), 1.75);
    EXPECT_FALSE(item("artworkCropApply")->isEnabled());
    ASSERT_TRUE(QMetaObject::invokeMethod(editor, "reloadArtwork"));
    ASSERT_TRUE(waitFor([&] { return !editor->property("reloading").toBool(); }));
    EXPECT_FALSE(editor->property("needsReload").toBool());
    EXPECT_EQ(editor->property("originalArtwork").toMap().value("banner_attachment_id"),
        app->channelDetails(id).value("banner_attachment_id"));
    EXPECT_DOUBLE_EQ(editor->property("zoom").toDouble(), 1.75);
}

TEST_F(ArtworkConflicts, OfflineRetainsCropAndDisablesApplyAndReload)
{
    open();
    const auto preview = editor->property("preview");
    editor->setProperty("needsReload", true);
    server.stop();
    ASSERT_TRUE(waitFor([&] { return app->state() != "connected"; }));
    EXPECT_TRUE(app->ready());
    EXPECT_TRUE(editor->property("visible").toBool());
    EXPECT_EQ(editor->property("preview"), preview);
    EXPECT_FALSE(item("artworkCropApply")->isEnabled());
    EXPECT_FALSE(item("artworkCropReload")->isEnabled());
}
TEST_F(ArtworkConflicts, DeletedSourceDisablesApplyAndControllerRejectsBeforeImageWork)
{
    open();
    const auto preview = editor->property("preview");
    const auto fingerprint = editor->property("fingerprint").toString();
    const auto url = editor->property("fileUrl").toUrl();
    ASSERT_TRUE(daemon.call("channel.delete", {{"channel", id}}).ok);
    ASSERT_TRUE(waitFor([&] { return app->channelDetails(id).isEmpty(); }));
    EXPECT_FALSE(item("artworkCropApply")->isEnabled());
    EXPECT_FALSE(item("artworkCropReload")->isEnabled());
    EXPECT_EQ(editor->property("preview"), preview);
    QString failure;
    const auto connection = QObject::connect(app.get(), &client::AppController::administrationFinished, app.get(),
        [&](const QString& operation, const QString& target, const QString& error, const QVariantMap&) {
            if (operation == "channel.artwork.crop" && target == id)
                failure = error;
        });
    app->setChannelArtworkCrop(id, "icon", url, 0.5, 0.5, 1, fingerprint);
    EXPECT_FALSE(failure.isEmpty());
    QObject::disconnect(connection);
}

TEST_F(ArtworkConflicts, CheckedRemovalConflictAndArtworkReloadPreserveSettingsTextDraft)
{
    ASSERT_TRUE(
        daemon.call("channel.artwork.set", {{"channel", id}, {"kind", "icon"}, {"file", image().toLocalFile()}}).ok);
    ASSERT_TRUE(waitFor([&] { return app->channelDetails(id).value("icon_attachment_id").toString() != "0"; }));
    auto* settings = object->findChild<QObject*>("settingsEditor");
    ASSERT_TRUE(settings);
    ASSERT_TRUE(QMetaObject::invokeMethod(settings, "openFor", Q_ARG(QVariant, id)));
    ASSERT_TRUE(waitFor([&] { return settings->property("opened").toBool(); }));
    auto* name = settings->findChild<QObject*>("channelName");
    ASSERT_TRUE(name);
    name->setProperty("text", "retained text draft");
    ASSERT_TRUE(QMetaObject::invokeMethod(settings, "selectSection", Q_ARG(QVariant, 1)));
    // Simulate a displayed baseline racing with another administrator's edit.
    auto stale = settings->property("details").toMap();
    stale["icon_attachment_id"] = "99999999";
    settings->setProperty("details", stale);
    ASSERT_TRUE(QMetaObject::invokeMethod(settings, "removeArtwork", Q_ARG(QVariant, "icon")));
    ASSERT_TRUE(waitFor([&] { return !settings->property("artworkSaving").toBool(); }));
    EXPECT_TRUE(settings->property("artworkNeedsReload").toBool());
    EXPECT_FALSE(settings->property("artworkStatus").toString().isEmpty());
    EXPECT_EQ(name->property("text").toString(), "retained text draft");
    EXPECT_TRUE(settings->property("dirty").toBool());
    auto* artworkReload = item("channelReloadArtwork");
    ASSERT_TRUE(artworkReload);
    ASSERT_TRUE(artworkReload->isEnabled());
    QTest::qWait(100); // Let the newly visible status/reload row finish layout.
    artworkReload->forceActiveFocus(Qt::TabFocusReason);
    ASSERT_TRUE(artworkReload->hasActiveFocus());
    QTest::qWait(100);
    capture("settings-conflict",
        {"channelReloadArtwork", "channelArtworkStatus", "channelSaveDetails", "channelCloseDetails"});
    ASSERT_TRUE(activateNativeWindow(window));
    QTest::keyClick(window, Qt::Key_Space);
    ASSERT_TRUE(waitFor([&] { return !settings->property("artworkReloading").toBool(); }));
    EXPECT_FALSE(settings->property("artworkNeedsReload").toBool());
    EXPECT_EQ(settings->property("details").toMap().value("icon_attachment_id"),
        app->channelDetails(id).value("icon_attachment_id"));
    EXPECT_EQ(name->property("text").toString(), "retained text draft");
    EXPECT_TRUE(settings->property("dirty").toBool());
    capture("settings-reloaded", {"channelArtworkStatus", "channelSaveDetails", "channelCloseDetails"});
}

TEST_F(ArtworkConflicts, LastArtworkRemovalRetainsCropAndRenewsFingerprintAfterSameScopeEpoch)
{
    ASSERT_TRUE(
        daemon.call("channel.artwork.set", {{"channel", id}, {"kind", "icon"}, {"file", image().toLocalFile()}}).ok);
    ASSERT_TRUE(waitFor([&] { return app->channelDetails(id).value("icon_attachment_id").toString() != "0"; }));
    open();
    editor->setProperty("focalX", 0.25);
    editor->setProperty("focalY", 0.75);
    editor->setProperty("zoom", 1.75);
    const auto preview = editor->property("preview");
    const auto url = editor->property("fileUrl");
    const auto fingerprint = editor->property("fingerprint").toString();
    const auto scope = app->artworkScopeFingerprint();
    const auto generation = app->artworkGeneration();
    ASSERT_TRUE(daemon.call("channel.artwork.set", {{"channel", id}, {"kind", "icon"}, {"file", ""}}).ok);
    ASSERT_TRUE(waitFor([&] { return app->artworkGeneration() != generation; }));
    EXPECT_EQ(app->artworkScopeFingerprint(), scope);
    EXPECT_TRUE(editor->property("visible").toBool());
    EXPECT_TRUE(editor->property("needsReload").toBool());
    EXPECT_EQ(editor->property("preview"), preview);
    EXPECT_EQ(editor->property("fileUrl"), url);
    EXPECT_DOUBLE_EQ(editor->property("focalX").toDouble(), 0.25);
    EXPECT_DOUBLE_EQ(editor->property("focalY").toDouble(), 0.75);
    EXPECT_DOUBLE_EQ(editor->property("zoom").toDouble(), 1.75);
    ASSERT_TRUE(waitFor([&] { return !editor->property("fingerprint").toString().isEmpty(); }));
    EXPECT_NE(editor->property("fingerprint").toString(), fingerprint);
    EXPECT_FALSE(item("artworkCropApply")->isEnabled());
    ASSERT_TRUE(QMetaObject::invokeMethod(editor, "reloadArtwork"));
    ASSERT_TRUE(waitFor([&] { return !editor->property("reloading").toBool(); }));
    EXPECT_FALSE(editor->property("needsReload").toBool());
    EXPECT_TRUE(item("artworkCropApply")->isEnabled());
}

TEST_F(ArtworkConflicts, OwnLastArtworkRemovalCompletesAndRetainsSettingsTextDraft)
{
    ASSERT_TRUE(
        daemon.call("channel.artwork.set", {{"channel", id}, {"kind", "icon"}, {"file", image().toLocalFile()}}).ok);
    ASSERT_TRUE(waitFor([&] { return app->channelDetails(id).value("icon_attachment_id").toString() != "0"; }));
    auto* settings = object->findChild<QObject*>("settingsEditor");
    ASSERT_TRUE(settings);
    ASSERT_TRUE(QMetaObject::invokeMethod(settings, "openFor", Q_ARG(QVariant, id)));
    ASSERT_TRUE(waitFor([&] { return settings->property("opened").toBool(); }));
    auto* name = settings->findChild<QObject*>("channelName");
    ASSERT_TRUE(name);
    name->setProperty("text", "draft survives last artwork removal");
    const auto generation = app->artworkGeneration();
    const auto scope = app->artworkScopeFingerprint();
    ASSERT_TRUE(QMetaObject::invokeMethod(settings, "removeArtwork", Q_ARG(QVariant, "icon")));
    ASSERT_TRUE(waitFor([&] { return !settings->property("artworkSaving").toBool(); }));
    ASSERT_TRUE(waitFor([&] { return app->artworkGeneration() != generation; }));
    EXPECT_EQ(app->artworkScopeFingerprint(), scope);
    EXPECT_TRUE(settings->property("visible").toBool());
    EXPECT_EQ(name->property("text").toString(), "draft survives last artwork removal");
    EXPECT_TRUE(settings->property("dirty").toBool());
    EXPECT_FALSE(settings->property("artworkNeedsReload").toBool());
    EXPECT_EQ(settings->property("artworkStatus").toString(), "Artwork removed.");
    EXPECT_EQ(app->channelDetails(id).value("icon_attachment_id").toString(), "0");
}

TEST_F(ArtworkConflicts, UnchangedSyntheticEpochSignalStillCancelsCrop)
{
    open();
    ASSERT_TRUE(QMetaObject::invokeMethod(app.get(), "artworkGenerationChanged"));
    EXPECT_FALSE(editor->property("uploading").toBool());
    ASSERT_TRUE(waitFor([&] { return !editor->property("visible").toBool(); }));
}

} // namespace
int main(int argc, char** argv)
{
    if (!qEnvironmentVariableIsSet("OMACHAT_TEST_WAYLAND"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("OMACHAT_THEME", "builtin");
    QTemporaryDir dataHome;
    qputenv("XDG_DATA_HOME", dataHome.path().toUtf8());
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
