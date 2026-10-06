#include "Harness.hpp"
#include "controllers/AppController.hpp"
#include "platform/ThemeProvider.hpp"
#include "views/AudioLevels.hpp"
#include "views/VideoFrameItem.hpp"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
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
QQuickItem* namedControl(QQuickItem* root, const QString& name)
{
    if (root->objectName() == name && root->isVisible())
        return root;
    for (auto* child : root->childItems())
        if (auto* found = namedControl(child, name))
            return found;
    return nullptr;
}
struct ServerCrop : ::testing::Test {
    TestServer server;
    TestDaemon daemon{"server-crop"};
    client::ThemeProvider theme{nullptr};
    QTemporaryDir files;
    std::unique_ptr<client::AppController> app;
    QString id, path;
    void SetUp() override
    {
        ASSERT_TRUE(server.start());
        ASSERT_TRUE(daemon.start());
        ASSERT_TRUE(daemon.registerOn(server, "server-crop", "test-password"));
        ASSERT_TRUE(daemon.call("server.create", {{"name", "Server crop"}}).ok);
        qputenv("OMACHAT_SOCKET",
            QString("/tmp/omachat-test-server-crop-%1.sock").arg(QCoreApplication::applicationPid()).toUtf8());
        qputenv("XDG_CACHE_HOME", files.path().toUtf8());
        config::ClientConfig config;
        config.startup.launchDaemon = false;
        app = std::make_unique<client::AppController>(config);
        app->start();
        ASSERT_TRUE(waitFor([&] { return app->canManageServer(); }));
        id = app->selectedServerId();
        path = files.filePath("source.png");
        QImage source(256, 128, QImage::Format_RGB32);
        source.fill(Qt::red);
        for (int y = 0; y < source.height(); ++y)
            for (int x = source.width() / 2; x < source.width(); ++x)
                source.setPixelColor(x, y, Qt::blue);
        ASSERT_TRUE(source.save(path));
    }
    void TearDown() override
    {
        QThreadPool::globalInstance()->waitForDone();
        app.reset();
        qunsetenv("OMACHAT_SOCKET");
        qunsetenv("XDG_CACHE_HOME");
    }
    QString prepare()
    {
        QString hash;
        const auto connection = QObject::connect(app.get(), &client::AppController::artworkCropPrepared,
            [&](int, const QString&, const QString& error, const QString& fingerprint, int, int) {
                EXPECT_TRUE(error.isEmpty()) << error.toStdString();
                hash = fingerprint;
            });
        app->prepareArtworkCrop(1, QUrl::fromLocalFile(path), "icon");
        EXPECT_TRUE(waitFor([&] { return !hash.isEmpty(); }));
        QObject::disconnect(connection);
        return hash;
    }
};
} // namespace

TEST_F(ServerCrop, ProductionDialogUploadsCanonicalIconAndBanner)
{
    for (bool light : {false, true}) {
        if (light) {
            QFile colors(files.filePath("crop-light.toml"));
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
            ServerSettingsDialog { objectName: "settings" }
        })",
                QUrl());
            std::unique_ptr<QObject> object(component.create());
            ASSERT_TRUE(object) << component.errorString().toStdString();
            auto* settings = object->findChild<QObject*>("settings");
            ASSERT_TRUE(settings);
            ASSERT_TRUE(QMetaObject::invokeMethod(settings, "open"));
            auto* crop = object->findChild<QObject*>("serverArtworkCropDialog");
            ASSERT_TRUE(crop);
            EXPECT_EQ(crop->property("target").toString(), "server");
            auto* window = qobject_cast<QQuickWindow*>(object.get());
            ASSERT_TRUE(window);
            window->resize(compact ? 720 : 1440, compact ? 460 : 900);
            QTest::qWait(150); // Let Wayland map/configure before compositor dispatch.
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
            ASSERT_TRUE(waitFor([&] {
                return std::abs(window->width() - (compact ? 720 : 1440)) <= 1
                    && window->height() == (compact ? 460 : 900);
            }));
            QTest::qWait(150);
            for (const QString& kind : {QString("icon"), QString("banner")}) {
                const QString key = kind + "_attachment_id";
                const QString previous = app->serverDetails(id).value(key).toString();
                ASSERT_TRUE(QMetaObject::invokeMethod(crop, "openFor", Q_ARG(QVariant, id), Q_ARG(QVariant, kind),
                    Q_ARG(QVariant, QUrl::fromLocalFile(path))));
                ASSERT_TRUE(waitFor([&] { return !crop->property("preview").toString().isEmpty(); }));
                EXPECT_TRUE(crop->property("title").toString().contains("server"));
                QTest::qWait(180);
                const QImage capture = window->grabWindow();
                ASSERT_EQ(capture.size(),
                    QSize(qRound(window->width() * window->devicePixelRatio()),
                        qRound(window->height() * window->devicePixelRatio())));
                ASSERT_TRUE(capture.save(QString("/tmp/omachat-server-crop-%1-%2-%3.png")
                        .arg(light ? "light" : "dark", compact ? "compact" : "wide", kind)));
                crop->setProperty("focalX", 1.0);
                auto* apply = namedControl(window->contentItem(), "artworkCropApply");
                ASSERT_TRUE(apply);
                ASSERT_TRUE(apply->isEnabled());
                apply->forceActiveFocus();
                QTest::keyClick(window, Qt::Key_Space);
                ASSERT_TRUE(waitFor([&] { return !crop->property("uploading").toBool(); }));
                EXPECT_TRUE(crop->property("error").toString().isEmpty());
                ASSERT_TRUE(waitFor([&] { return app->serverDetails(id).value(key).toString() != previous; }));
                const QString attachment = app->serverDetails(id).value(key).toString();
                app->requestPreview(attachment, kind + ".png", 0);
                ASSERT_TRUE(waitFor([&] { return !app->previews().value(attachment).toString().isEmpty(); }));
                const QImage image(QUrl(app->previews().value(attachment).toString()).toLocalFile());
                EXPECT_EQ(image.size(), kind == "icon" ? QSize(512, 512) : QSize(1024, 256));
                if (kind == "icon") {
                    EXPECT_GT(image.pixelColor(256, 256).blue(), 200);
                }
            }
        }
    }
}

TEST_F(ServerCrop, ChangedSourceAndWrongSelectedServerRejectWithoutMutation)
{
    const QString fingerprint = prepare();
    ASSERT_FALSE(fingerprint.isEmpty());
    int finished = 0;
    QString error;
    QObject::connect(app.get(), &client::AppController::administrationFinished, app.get(),
        [&](const QString& op, const QString&, const QString& failure, const QVariantMap&) {
            if (op == "server.artwork.crop") {
                ++finished;
                error = failure;
            }
        });
    QImage replacement(256, 128, QImage::Format_RGB32);
    replacement.fill(Qt::green);
    ASSERT_TRUE(replacement.save(path));
    app->setServerArtworkCrop(id, "icon", QUrl::fromLocalFile(path), 0.5, 0.5, 1, fingerprint);
    ASSERT_TRUE(waitFor([&] { return finished == 1; }));
    EXPECT_TRUE(error.contains("changed"));
    app->setServerArtworkCrop("999999999999", "icon", QUrl::fromLocalFile(path), 0.5, 0.5, 1, fingerprint);
    EXPECT_EQ(finished, 2);
    EXPECT_FALSE(error.isEmpty());
    EXPECT_EQ(app->serverDetails(id).value("icon_attachment_id").toString(), "0");
}

TEST_F(ServerCrop, SupersededWorkerCannotCompleteOrMutateServer)
{
    const QString fingerprint = prepare();
    ASSERT_FALSE(fingerprint.isEmpty());
    auto* pool = QThreadPool::globalInstance();
    const int threads = pool->maxThreadCount();
    pool->waitForDone();
    pool->setMaxThreadCount(1);
    QSemaphore blocked, release;
    pool->start([&] {
        blocked.release();
        release.acquire();
    });
    ASSERT_TRUE(blocked.tryAcquire(1, 5000));
    const auto cleanup = qScopeGuard([&] {
        release.release();
        pool->waitForDone();
        pool->setMaxThreadCount(threads);
    });
    int finished = 0;
    QObject::connect(app.get(), &client::AppController::administrationFinished, app.get(),
        [&](const QString& op, const QString&, const QString&, const QVariantMap&) {
            if (op == "server.artwork.crop")
                ++finished;
        });
    app->setServerArtworkCrop(id, "icon", QUrl::fromLocalFile(path), 1, 0.5, 1, fingerprint);
    // A newer rejected operation must suppress the queued older operation too.
    app->setServerArtworkCrop(id, "icon", QUrl("https://invalid/image.png"), 1, 0.5, 1, fingerprint);
    EXPECT_EQ(finished, 1);
    release.release();
    pool->waitForDone();
    QTest::qWait(200);
    EXPECT_EQ(finished, 1);
    EXPECT_EQ(app->serverDetails(id).value("icon_attachment_id").toString(), "0");
}

TEST_F(ServerCrop, PendingGenerationChangeClosesDialogAndSuppressesOldCompletion)
{
    ASSERT_TRUE(daemon.call("server.artwork.set", {{"server", id}, {"kind", "icon"}, {"file", path}}).ok);
    ASSERT_TRUE(waitFor([&] { return app->serverDetails(id).value("icon_attachment_id").toString() != "0"; }));
    const quint64 generation = app->artworkGeneration();
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls
import OmaChat
ApplicationWindow { width: 720; height: 460; visible: true
    ArtworkCropDialog { objectName: "crop"; target: "server" }
})",
        QUrl());
    std::unique_ptr<QObject> object(component.create());
    ASSERT_TRUE(object) << component.errorString().toStdString();
    auto* crop = object->findChild<QObject*>("crop");
    ASSERT_TRUE(crop);
    const auto open = [&] {
        return QMetaObject::invokeMethod(
            crop, "openFor", Q_ARG(QVariant, id), Q_ARG(QVariant, "icon"), Q_ARG(QVariant, QUrl::fromLocalFile(path)));
    };
    ASSERT_TRUE(open());
    ASSERT_TRUE(waitFor([&] { return !crop->property("fingerprint").toString().isEmpty(); }));
    const QString hash = crop->property("fingerprint").toString();
    auto* pool = QThreadPool::globalInstance();
    pool->waitForDone();
    const int threads = pool->maxThreadCount();
    pool->setMaxThreadCount(1);
    QSemaphore entered, release;
    pool->start([&] {
        entered.release();
        release.acquire();
    });
    ASSERT_TRUE(entered.tryAcquire(1, 5000));
    const auto cleanup = qScopeGuard([&] {
        release.release();
        pool->waitForDone();
        pool->setMaxThreadCount(threads);
    });
    int finished = 0;
    QObject::connect(app.get(), &client::AppController::administrationFinished, app.get(),
        [&](const QString& op, const QString&, const QString&, const QVariantMap&) {
            if (op == "server.artwork.crop")
                ++finished;
        });
    crop->setProperty("uploading", true);
    app->setServerArtworkCrop(id, "icon", QUrl::fromLocalFile(path), 1, 0.5, 1, hash);
    ASSERT_TRUE(daemon.call("server.artwork.set", {{"server", id}, {"kind", "icon"}, {"file", ""}}).ok);
    ASSERT_TRUE(waitFor([&] { return app->artworkGeneration() != generation; }));
    EXPECT_FALSE(crop->property("uploading").toBool());
    ASSERT_TRUE(waitFor([&] { return !crop->property("visible").toBool(); }));
    ASSERT_TRUE(open());
    crop->setProperty("uploading", true);
    release.release();
    pool->waitForDone();
    QCoreApplication::processEvents();
    EXPECT_EQ(finished, 0);
    EXPECT_TRUE(crop->property("uploading").toBool());
    EXPECT_EQ(app->serverDetails(id).value("icon_attachment_id").toString(), "0");
}

TEST_F(ServerCrop, PreviewFromAnotherAccountCannotUploadServerArtwork)
{
    const QString hash = prepare();
    ASSERT_FALSE(hash.isEmpty());
    const auto invite = daemon.call("invite.create", {{"server", id}});
    ASSERT_TRUE(invite.ok);
    ASSERT_TRUE(daemon.registerOn(server, "server-crop-reader", "test-password"));
    ASSERT_TRUE(daemon.call("server.join", {{"invite", invite.result.value("uri")}}).ok);
    ASSERT_TRUE(
        waitFor([&] { return app->selfUsername() == "server-crop-reader" && !app->serverDetails(id).isEmpty(); }));
    app->selectServer(id);
    ASSERT_TRUE(waitFor([&] { return app->selectedServerId() == id; }));
    int finished = 0;
    QString error;
    QObject::connect(app.get(), &client::AppController::administrationFinished, app.get(),
        [&](const QString& op, const QString&, const QString& failure, const QVariantMap&) {
            if (op == "server.artwork.crop") {
                ++finished;
                error = failure;
            }
        });
    app->setServerArtworkCrop(id, "icon", QUrl::fromLocalFile(path), 0.5, 0.5, 1, hash);
    ASSERT_TRUE(waitFor([&] { return finished == 1; }));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_EQ(app->serverDetails(id).value("icon_attachment_id").toString(), "0");
}

TEST_F(ServerCrop, PreparedPreviewCannotBeReusedForAnotherSelectedServer)
{
    const QString hash = prepare();
    ASSERT_FALSE(hash.isEmpty());
    const auto created = daemon.call("server.create", {{"name", "Other server crop"}});
    ASSERT_TRUE(created.ok);
    const QString other = created.result.value("id").toString();
    ASSERT_FALSE(other.isEmpty());
    ASSERT_TRUE(waitFor([&] { return !app->serverDetails(other).isEmpty(); }));
    app->selectServer(other);
    ASSERT_TRUE(waitFor([&] { return app->selectedServerId() == other && app->canManageServer(); }));
    int finished = 0;
    QString error;
    QObject::connect(app.get(), &client::AppController::administrationFinished, app.get(),
        [&](const QString& op, const QString&, const QString& failure, const QVariantMap&) {
            if (op == "server.artwork.crop") {
                ++finished;
                error = failure;
            }
        });
    app->setServerArtworkCrop(other, "icon", QUrl::fromLocalFile(path), 0.5, 0.5, 1, hash);
    ASSERT_TRUE(waitFor([&] { return finished == 1; }));
    EXPECT_TRUE(error.contains("changed"));
    EXPECT_EQ(app->serverDetails(other).value("icon_attachment_id").toString(), "0");
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
