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
#include <QJsonDocument>
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
struct CreationCrop : ::testing::Test {
    TestServer server;
    TestDaemon daemon{"creation-crop"};
    client::ThemeProvider theme{nullptr};
    QTemporaryDir files;
    std::unique_ptr<client::AppController> app;
    QString id, path;
    void SetUp() override
    {
        theme.setReducedMotion(true);
        ASSERT_TRUE(server.start());
        ASSERT_TRUE(daemon.start());
        ASSERT_TRUE(daemon.registerOn(server, "creation-crop", "test-password"));
        ASSERT_TRUE(daemon.call("server.create", {{"name", "Creation crop"}}).ok);
        qputenv("OMACHAT_SOCKET",
            QString("/tmp/omachat-test-creation-crop-%1.sock").arg(QCoreApplication::applicationPid()).toUtf8());
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

TEST_F(CreationCrop, ApprovedCropsUploadCanonicalPixelsAndOutliveDraftRelease)
{
    QUrl token;
    QString failure;
    const auto staged = QObject::connect(
        app.get(), &client::AppController::artworkCropStaged, [&](int, const QUrl& value, const QString& error) {
            token = value;
            failure = error;
        });
    app->stageArtworkCrop(10, QUrl::fromLocalFile(path), "icon", 1, 0.5, 1, prepare(), true);
    ASSERT_TRUE(waitFor([&] { return !token.isEmpty() || !failure.isEmpty(); }));
    ASSERT_TRUE(failure.isEmpty()) << failure.toStdString();
    EXPECT_EQ(token.scheme(), "omachat-artwork");
    EXPECT_FALSE(token.isLocalFile());
    QString createdId;
    bool done = false;
    QObject::connect(app.get(), &client::AppController::administrationFinished,
        [&](const QString& op, const QString& channel, const QString& error, const QVariantMap& saved) {
            if (op != "channel.create")
                return;
            done = true;
            failure = error;
            createdId = channel;
            EXPECT_TRUE(saved.value("created").toBool());
        });
    app->createChannel("approved-crop", "text", "0", "", "", token, {});
    // Closing/replacing a draft must not delete the private crop under the upload.
    app->discardStagedArtwork(token);
    ASSERT_TRUE(waitFor([&] { return done; }));
    ASSERT_TRUE(failure.isEmpty()) << failure.toStdString();
    const QString attachment = app->channelDetails(createdId).value("icon_attachment_id").toString();
    ASSERT_FALSE(attachment.isEmpty());
    ASSERT_NE(attachment, "0");
    app->requestPreview(attachment, "icon.png", 0);
    ASSERT_TRUE(waitFor([&] { return !app->previews().value(attachment).toString().isEmpty(); }));
    QImage output(QUrl(app->previews().value(attachment).toString()).toLocalFile());
    ASSERT_EQ(output.size(), QSize(128, 128));
    EXPECT_EQ(output.pixelColor(64, 64), QColor(Qt::blue));
    EXPECT_TRUE(output.textKeys().isEmpty());
    QObject::disconnect(staged);
}

TEST_F(CreationCrop, ReplacedSourceAndWrongKindTokensCannotCreate)
{
    const QString fingerprint = prepare();
    QImage changed(256, 128, QImage::Format_RGB32);
    changed.fill(Qt::green);
    ASSERT_TRUE(changed.save(path));
    QString failure;
    QUrl token;
    bool settled = false;
    QObject::connect(
        app.get(), &client::AppController::artworkCropStaged, [&](int, const QUrl& value, const QString& error) {
            token = value;
            failure = error;
            settled = true;
        });
    app->stageArtworkCrop(20, QUrl::fromLocalFile(path), "icon", 0.5, 0.5, 1, fingerprint);
    ASSERT_TRUE(waitFor([&] { return settled; }));
    EXPECT_TRUE(token.isEmpty());
    EXPECT_TRUE(failure.contains("changed"));
    settled = false;
    app->stageArtworkCrop(21, QUrl::fromLocalFile(path), "banner", 0.5, 0.5, 1, prepare());
    ASSERT_TRUE(waitFor([&] { return settled; }));
    ASSERT_TRUE(failure.isEmpty());
    ASSERT_FALSE(token.isEmpty());
    bool finished = false;
    QObject::connect(app.get(), &client::AppController::administrationFinished,
        [&](const QString& op, const QString&, const QString& error, const QVariantMap&) {
            if (op == "channel.create") {
                failure = error;
                finished = true;
            }
        });
    app->createChannel("wrong-kind", "text", "0", "", "", token, {});
    EXPECT_TRUE(finished);
    EXPECT_FALSE(failure.isEmpty());
    app->discardStagedArtwork(token);
}

TEST_F(CreationCrop, RejectedCreateRetainsCropForDeliberateRetry)
{
    QUrl token;
    QString failure;
    QObject::connect(
        app.get(), &client::AppController::artworkCropStaged, [&](int, const QUrl& value, const QString& error) {
            token = value;
            failure = error;
        });
    app->stageArtworkCrop(30, QUrl::fromLocalFile(path), "icon", 0.5, 0.5, 1, prepare());
    ASSERT_TRUE(waitFor([&] { return !token.isEmpty() || !failure.isEmpty(); }));
    ASSERT_TRUE(failure.isEmpty());
    int finished = 0;
    bool created = false;
    QObject::connect(app.get(), &client::AppController::administrationFinished,
        [&](const QString& op, const QString&, const QString& error, const QVariantMap& saved) {
            if (op == "channel.create") {
                failure = error;
                created = saved.value("created").toBool();
                ++finished;
            }
        });
    app->createChannel("", "text", "0", "", "", token, {});
    ASSERT_TRUE(waitFor([&] { return finished == 1; }));
    EXPECT_FALSE(created);
    EXPECT_FALSE(failure.isEmpty());
    app->createChannel("retry-approved", "text", "0", "", "", token, {});
    ASSERT_TRUE(waitFor([&] { return finished == 2; }));
    EXPECT_TRUE(created);
    EXPECT_TRUE(failure.isEmpty()) << failure.toStdString();
    app->discardStagedArtwork(token);
}

TEST_F(CreationCrop, ServerSwitchAwayAndBackInvalidatesStagedCrop)
{
    QUrl token;
    QString failure;
    QObject::connect(
        app.get(), &client::AppController::artworkCropStaged, [&](int, const QUrl& value, const QString& error) {
            token = value;
            failure = error;
        });
    app->stageArtworkCrop(40, QUrl::fromLocalFile(path), "icon", 0.5, 0.5, 1, prepare());
    ASSERT_TRUE(waitFor([&] { return !token.isEmpty() || !failure.isEmpty(); }));
    ASSERT_TRUE(failure.isEmpty());
    const auto other = daemon.call("server.create", {{"name", "Different crop destination"}});
    ASSERT_TRUE(other.ok);
    const QString otherId = other.result.value("id").toString();
    ASSERT_TRUE(waitFor([&] { return app->servers()->indexOf("itemId", otherId) >= 0; }));
    app->selectServer(otherId);
    ASSERT_EQ(app->selectedServerId(), otherId);
    app->selectServer(id);
    ASSERT_EQ(app->selectedServerId(), id);
    bool finished = false;
    QObject::connect(app.get(), &client::AppController::administrationFinished,
        [&](const QString& op, const QString&, const QString& error, const QVariantMap&) {
            if (op == "channel.create") {
                failure = error;
                finished = true;
            }
        });
    app->createChannel("cross-server-crop", "text", "0", "", "", token, {});
    EXPECT_TRUE(finished);
    EXPECT_FALSE(failure.isEmpty());
}

TEST_F(CreationCrop, ApprovedChoiceSurvivesControllerRestartAndProductionReview)
{
    QUrl token;
    const auto staged = QObject::connect(
        app.get(), &client::AppController::artworkCropStaged, [&](int, const QUrl& value, const QString& error) {
            EXPECT_TRUE(error.isEmpty()) << error.toStdString();
            token = value;
        });
    app->stageArtworkCrop(100, QUrl::fromLocalFile(path), "icon", 1, 0.25, 2, prepare());
    ASSERT_TRUE(waitFor([&] { return !token.isEmpty(); }));
    QThreadPool::globalInstance()->waitForDone();
    QObject::disconnect(staged);
    app.reset();
    config::ClientConfig config;
    config.startup.launchDaemon = false;
    app = std::make_unique<client::AppController>(config);
    app->start();
    ASSERT_TRUE(waitFor([&] { return app->canManageServer(); }));
    ASSERT_EQ(app->selectedServerId(), id);
    for (bool light : {false, true}) {
        if (light) {
            QFile colors(files.filePath("memory-light.toml"));
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
                ApplicationWindow { width: 720; height: 460; visible: true; color: Theme.background
                    ArtworkCropDialog { objectName: "crop"; target: "creation" }
                })",
                QUrl());
            std::unique_ptr<QObject> object(component.create());
            ASSERT_TRUE(object) << component.errorString().toStdString();
            auto* crop = object->findChild<QObject*>("crop");
            auto* window = qobject_cast<QQuickWindow*>(object.get());
            ASSERT_TRUE(crop);
            ASSERT_TRUE(window);
            const int width = compact ? 720 : 1440, height = compact ? 460 : 900;
            window->resize(width, height);
            ASSERT_TRUE(prepareNativeWindow(window, width, height));
            ASSERT_TRUE(QMetaObject::invokeMethod(crop, "openFor", Q_ARG(QVariant, QString()),
                Q_ARG(QVariant, QString("icon")), Q_ARG(QVariant, QUrl::fromLocalFile(path))));
            ASSERT_TRUE(waitFor([&] { return !crop->property("preview").toString().isEmpty(); }));
            EXPECT_EQ(crop->property("focalX").toDouble(), 1);
            EXPECT_EQ(crop->property("focalY").toDouble(), 0.25);
            EXPECT_EQ(crop->property("zoom").toDouble(), 2);
            EXPECT_TRUE(crop->property("error").toString().contains("restored on this device"));
            auto* apply = namedControl(window->contentItem(), "artworkCropApply");
            auto* reset = namedControl(window->contentItem(), "artworkCropReset");
            ASSERT_TRUE(apply && reset);
            EXPECT_TRUE(apply->isEnabled());
            for (auto* item : {apply, reset}) {
                const QRectF bounds = item->mapRectToScene({0, 0, item->width(), item->height()});
                EXPECT_GE(bounds.left(), 0);
                EXPECT_GE(bounds.top(), 0);
                EXPECT_LE(bounds.right(), window->width());
                EXPECT_LE(bounds.bottom(), window->height());
            }
            ASSERT_TRUE(waitFor([&] { return crop->property("opacity").toDouble() == 1; }));
            QTest::qWait(100);
            if (qEnvironmentVariableIsSet("OMACHAT_CAPTURE_DIR")) {
                QDir captures(qEnvironmentVariable("OMACHAT_CAPTURE_DIR"));
                ASSERT_TRUE(captures.mkpath("."));
                ASSERT_TRUE(window->grabWindow().save(captures.filePath(
                    QString("crop-memory-%1-%2.png").arg(light ? "light" : "dark", compact ? "compact" : "wide"))));
            }
            reset->forceActiveFocus();
            ASSERT_TRUE(activateNativeWindow(window));
            QTest::keyClick(window, Qt::Key_Space);
            EXPECT_EQ(crop->property("focalX").toDouble(), 0.5);
            EXPECT_EQ(crop->property("focalY").toDouble(), 0.5);
            EXPECT_EQ(crop->property("zoom").toDouble(), 1);
            ASSERT_TRUE(QMetaObject::invokeMethod(crop, "close"));
            ASSERT_TRUE(waitFor([&] { return !crop->property("visible").toBool(); }));
            // Cancel/Reset alone never overwrite the last explicitly approved choice.
            window->close();
        }
    }
}

TEST_F(CreationCrop, PendingRestoreDoesNotOverwriteNewKeyboardPosition)
{
    QUrl token;
    const auto staged = QObject::connect(
        app.get(), &client::AppController::artworkCropStaged, [&](int, const QUrl& value, const QString& error) {
            EXPECT_TRUE(error.isEmpty());
            token = value;
        });
    app->stageArtworkCrop(110, QUrl::fromLocalFile(path), "icon", 1, 0.25, 2, prepare());
    ASSERT_TRUE(waitFor([&] { return !token.isEmpty(); }));
    QThreadPool::globalInstance()->waitForDone();
    QObject::disconnect(staged);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import OmaChat
        ApplicationWindow { width: 720; height: 460; visible: true
            ArtworkCropDialog { objectName: "crop"; target: "creation" }
        })",
        QUrl());
    std::unique_ptr<QObject> object(component.create());
    ASSERT_TRUE(object) << component.errorString().toStdString();
    auto* crop = object->findChild<QObject*>("crop");
    auto* window = qobject_cast<QQuickWindow*>(object.get());
    ASSERT_TRUE(crop && window);
    ASSERT_TRUE(prepareNativeWindow(window, 720, 460));
    auto* pool = QThreadPool::globalInstance();
    const int threads = pool->maxThreadCount();
    pool->setMaxThreadCount(1);
    QSemaphore entered, release;
    pool->start([&] {
        entered.release();
        release.acquire();
    });
    const auto cleanup = qScopeGuard([&] {
        release.release();
        pool->waitForDone();
        pool->setMaxThreadCount(threads);
    });
    ASSERT_TRUE(entered.tryAcquire(1, 5000));
    ASSERT_TRUE(QMetaObject::invokeMethod(crop, "openFor", Q_ARG(QVariant, QString()), Q_ARG(QVariant, QString("icon")),
        Q_ARG(QVariant, QUrl::fromLocalFile(path))));
    ASSERT_TRUE(crop->property("preview").toString().isEmpty());
    auto* viewport = namedControl(window->contentItem(), "artworkCropViewport");
    ASSERT_TRUE(viewport);
    viewport->forceActiveFocus();
    ASSERT_TRUE(activateNativeWindow(window));
    QTest::keyClick(window, Qt::Key_Right);
    EXPECT_DOUBLE_EQ(crop->property("focalX").toDouble(), 0.52);
    release.release();
    ASSERT_TRUE(waitFor([&] { return !crop->property("preview").toString().isEmpty(); }));
    EXPECT_DOUBLE_EQ(crop->property("focalX").toDouble(), 0.52);
    EXPECT_DOUBLE_EQ(crop->property("focalY").toDouble(), 0.5);
    EXPECT_DOUBLE_EQ(crop->property("zoom").toDouble(), 1);
}

TEST_F(CreationCrop, RejectedSourceCannotOverwriteRememberedChoice)
{
    QUrl token;
    bool staged = false;
    QString failure;
    QObject::connect(
        app.get(), &client::AppController::artworkCropStaged, [&](int, const QUrl& value, const QString& error) {
            token = value;
            failure = error;
            staged = true;
        });
    const QString approvedFingerprint = prepare();
    app->stageArtworkCrop(101, QUrl::fromLocalFile(path), "icon", 1, 0.25, 2, approvedFingerprint);
    ASSERT_TRUE(waitFor([&] { return staged; }));
    ASSERT_TRUE(failure.isEmpty());
    QThreadPool::globalInstance()->waitForDone();
    staged = false;
    app->stageArtworkCrop(102, QUrl::fromLocalFile(path), "icon", 0, 0, 1, "wrong fingerprint");
    ASSERT_TRUE(waitFor([&] { return staged; }));
    EXPECT_FALSE(failure.isEmpty());
    QVariantMap remembered;
    bool prepared = false;
    QObject::connect(app.get(), &client::AppController::artworkCropPrepared,
        [&](int, const QString&, const QString& error, const QString&, int, int, const QVariantMap& choice) {
            EXPECT_TRUE(error.isEmpty());
            remembered = choice;
            prepared = true;
        });
    app->prepareArtworkCrop(103, QUrl::fromLocalFile(path), "icon");
    ASSERT_TRUE(waitFor([&] { return prepared; }));
    EXPECT_EQ(remembered.value("x").toDouble(), 1);
    EXPECT_EQ(remembered.value("zoom").toDouble(), 2);
    prepared = false;
    app->prepareArtworkCrop(104, QUrl::fromLocalFile(path), "banner");
    ASSERT_TRUE(waitFor([&] { return prepared; }));
    EXPECT_TRUE(remembered.isEmpty());
    QImage changed(256, 128, QImage::Format_RGB32);
    changed.fill(Qt::green);
    ASSERT_TRUE(changed.save(path));
    prepared = false;
    app->prepareArtworkCrop(105, QUrl::fromLocalFile(path), "icon");
    ASSERT_TRUE(waitFor([&] { return prepared; }));
    EXPECT_TRUE(remembered.isEmpty());
}

TEST_F(CreationCrop, ProductionCreationDialogApprovesCropAndProtectsDirtyDraft)
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
                    CreateChannelDialog { objectName: "creation" }
                })",
                QUrl());
            std::unique_ptr<QObject> object(component.create());
            ASSERT_TRUE(object) << component.errorString().toStdString();
            auto* dialog = object->findChild<QObject*>("creation");
            auto* window = qobject_cast<QQuickWindow*>(object.get());
            ASSERT_TRUE(dialog);
            ASSERT_TRUE(window);
            window->resize(compact ? 720 : 1440, compact ? 460 : 900);
            ASSERT_TRUE(prepareNativeWindow(window, compact ? 720 : 1440, compact ? 460 : 900));
            ASSERT_TRUE(QMetaObject::invokeMethod(dialog, "open"));
            auto* crop = dialog->findChild<QObject*>("artworkCropDialog");
            ASSERT_TRUE(crop);
            ASSERT_EQ(crop->property("target").toString(), "creation");
            for (const QString& kind : {QString("icon"), QString("banner")}) {
                ASSERT_TRUE(QMetaObject::invokeMethod(crop, "openFor", Q_ARG(QVariant, QString()),
                    Q_ARG(QVariant, kind), Q_ARG(QVariant, QUrl::fromLocalFile(path))));
                ASSERT_TRUE(waitFor([&] { return !crop->property("preview").toString().isEmpty(); }));
                QTest::qWait(150);
                auto* apply = namedControl(window->contentItem(), "artworkCropApply");
                ASSERT_TRUE(apply);
                EXPECT_EQ(apply->property("text").toString(), "Use crop");
                ASSERT_TRUE(apply->isEnabled());
                ASSERT_TRUE(window->grabWindow().save(QString("/tmp/omachat-creation-crop-%1-%2-%3.png")
                        .arg(light ? "light" : "dark", compact ? "compact" : "wide", kind)));
                apply->forceActiveFocus();
                ASSERT_TRUE(activateNativeWindow(window));
                QTest::keyClick(window, Qt::Key_Space);
                ASSERT_TRUE(waitFor(
                    [&] { return !crop->property("visible").toBool() && dialog->property("activeFocus").toBool(); }));
                EXPECT_EQ(dialog->property((kind + "File").toUtf8()).toUrl().scheme(), "omachat-artwork");
            }
            EXPECT_TRUE(dialog->property("dirty").toBool());
            ASSERT_TRUE(activateNativeWindow(window));
            QTest::keyClick(window, Qt::Key_Escape);
            auto* confirm = dialog->findChild<QObject*>("discardChannelCreation");
            ASSERT_TRUE(confirm);
            ASSERT_TRUE(waitFor([&] { return confirm->property("visible").toBool(); }));
            EXPECT_TRUE(dialog->property("visible").toBool());
            ASSERT_TRUE(activateNativeWindow(window));
            QTest::keyClick(window, Qt::Key_Escape);
            ASSERT_TRUE(waitFor([&] { return !confirm->property("visible").toBool(); }));
            EXPECT_FALSE(dialog->property("iconFile").toUrl().isEmpty());
            EXPECT_FALSE(dialog->property("bannerFile").toUrl().isEmpty());
            ASSERT_TRUE(QMetaObject::invokeMethod(dialog, "close"));
            ASSERT_TRUE(waitFor([&] { return !dialog->property("visible").toBool(); }));
            EXPECT_TRUE(dialog->property("iconFile").toUrl().isEmpty());
            EXPECT_TRUE(dialog->property("bannerFile").toUrl().isEmpty());
            window->close();
            QTest::qWait(100);
        }
    }
}

TEST_F(CreationCrop, PartialArtworkFailurePreservesCreatedDialogAndPreventsDuplicate)
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import OmaChat
        ApplicationWindow { width: 720; height: 460; visible: true
            CreateChannelDialog { objectName: "creation" }
        })",
        QUrl());
    std::unique_ptr<QObject> object(component.create());
    ASSERT_TRUE(object) << component.errorString().toStdString();
    auto* dialog = object->findChild<QObject*>("creation");
    ASSERT_TRUE(dialog);
    ASSERT_TRUE(QMetaObject::invokeMethod(dialog, "open"));
    auto* name = dialog->findChild<QObject*>("channelName");
    ASSERT_TRUE(name);
    name->setProperty("text", "partial-crop-created");
    const auto before = daemon.call("channel.list", {{"server", id}});
    ASSERT_TRUE(before.ok);
    const int originalCount = before.result.value("channels").toArray().size();
    // Direct API compatibility also allows exercising a file disappearing before upload.
    dialog->setProperty("iconFile", QUrl::fromLocalFile(files.filePath("removed.png")));
    int finished = 0;
    QString createdId;
    QObject::connect(app.get(), &client::AppController::administrationFinished,
        [&](const QString& op, const QString& channel, const QString& error, const QVariantMap& saved) {
            if (op != "channel.create")
                return;
            ++finished;
            createdId = channel;
            EXPECT_TRUE(saved.value("created").toBool());
            EXPECT_FALSE(error.isEmpty());
        });
    ASSERT_TRUE(QMetaObject::invokeMethod(dialog, "create"));
    ASSERT_TRUE(waitFor([&] { return finished == 1 && !dialog->property("saving").toBool(); }));
    ASSERT_FALSE(createdId.isEmpty());
    EXPECT_TRUE(dialog->property("created").toBool());
    EXPECT_TRUE(dialog->property("visible").toBool());
    EXPECT_TRUE(dialog->property("saveError").toString().contains("Channel created"));
    auto* createButton = object->findChild<QObject*>("channelCreate");
    ASSERT_TRUE(createButton);
    EXPECT_FALSE(createButton->property("enabled").toBool());
    EXPECT_EQ(app->channelDetails(createdId).value("name").toString(), "partial-crop-created");
    ASSERT_TRUE(QMetaObject::invokeMethod(dialog, "create"));
    QTest::qWait(150);
    EXPECT_EQ(finished, 1);
    const auto after = daemon.call("channel.list", {{"server", id}});
    ASSERT_TRUE(after.ok);
    EXPECT_EQ(after.result.value("channels").toArray().size(), originalCount + 1);
    EXPECT_TRUE(dialog->property("saveError").toString().contains("Channel created"));
    ASSERT_TRUE(QMetaObject::invokeMethod(dialog, "requestClose"));
    ASSERT_TRUE(waitFor([&] { return !dialog->property("visible").toBool(); }));
}

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
