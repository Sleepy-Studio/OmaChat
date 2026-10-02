#include "controllers/AppController.hpp"
#include "omachat/config/ClientConfig.hpp"
#include "omachat/core/Log.hpp"
#include "omachat/core/Paths.hpp"
#include "omachat/core/Version.hpp"
#include "platform/SingleInstance.hpp"
#include "platform/ThemeProvider.hpp"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include <unistd.h>

using namespace omachat;

int main(int argc, char** argv)
{
    if (::geteuid() == 0) {
        std::fprintf(stderr, "omachat must not run as root\n");
        return 1;
    }

    QString configError;
    const auto config = config::ClientConfig::load(paths::configFile(), &configError);

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("omachat"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("OmaChat"));
    QGuiApplication::setApplicationVersion(QString::fromLatin1(kVersion));
    QGuiApplication::setDesktopFileName(QString::fromLatin1(kAppId));
    QGuiApplication::setWindowIcon(
        QIcon::fromTheme(QString::fromLatin1(kAppId), QIcon(QStringLiteral(":/qt/qml/OmaChat/icons/app.svg"))));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("OmaChat desktop client"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("uri"), QStringLiteral("Optional omachat:// link to open."));
    QCommandLineOption screenshot(QStringLiteral("screenshot"),
        QStringLiteral("Developer tool: save a screenshot after startup and exit."), QStringLiteral("file"));
    parser.addOption(screenshot);
    parser.process(app);

    log::initialize("omachat", log::Level::Info);
    if (!configError.isEmpty())
        OMA_WARN("gui", "config file has errors; using defaults", {"error", configError});

    client::SingleInstance instance;
    if (!parser.isSet(screenshot)) {
        if (instance.forwardToRunning(parser.positionalArguments()))
            return 0;
        instance.listen();
    }

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    client::ThemeProvider theme(nullptr);
    theme.setScale(config.ui.scale);
    theme.setReducedMotion(config.ui.reducedMotion);
    client::AppController controller(config);

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.loadFromModule("OmaChat", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());

    auto handleArgs = [&controller, window](const QStringList& args) {
        if (window) {
            window->show();
            window->raise();
            window->requestActivate();
        }
        for (const QString& a : args) {
            if (a.startsWith(QStringLiteral("omachat://")))
                QTimer::singleShot(0, &controller, [&controller, a] { controller.openLink(a); });
        }
    };
    QObject::connect(&instance, &client::SingleInstance::activated, &app, handleArgs);
    if (!parser.positionalArguments().isEmpty())
        QObject::connect(&controller, &client::AppController::statusChanged, &app,
            [handleArgs, args = parser.positionalArguments(), done = false, &controller]() mutable {
                if (!done && controller.ready()) {
                    done = true;
                    handleArgs(args);
                }
            });

    // CI screenshots stay self-contained. A developer can opt into an
    // isolated running daemon to capture real chat at different sizes.
    if (!parser.isSet(screenshot) || qEnvironmentVariableIsSet("OMACHAT_SCREENSHOT_LIVE"))
        controller.start();

    if (parser.isSet(screenshot) && window) {
        const int width = qEnvironmentVariableIntValue("OMACHAT_SCREENSHOT_WIDTH");
        const int height = qEnvironmentVariableIntValue("OMACHAT_SCREENSHOT_HEIGHT");
        if (width > 0 && height > 0)
            window->resize(width, height);
        const QString path = parser.value(screenshot);
        QTimer::singleShot(qEnvironmentVariableIntValue("OMACHAT_SCREENSHOT_DELAY_MS") > 0
                ? qEnvironmentVariableIntValue("OMACHAT_SCREENSHOT_DELAY_MS")
                : 2500,
            window, [window, path] {
                auto capture = [window, path] {
                    const QImage frame = window->grabWindow();
                    if (frame.isNull() || !frame.save(path)) {
                        std::fprintf(stderr, "omachat: could not save GUI screenshot\n");
                        QCoreApplication::exit(1);
                        return;
                    }
                    QCoreApplication::quit();
                };
                if (qEnvironmentVariableIsSet("OMACHAT_SCREENSHOT_SETTINGS")) {
                    auto* settings = window->findChild<QObject*>(QStringLiteral("settingsDialog"));
                    if (!settings || !QMetaObject::invokeMethod(settings, "open")) {
                        std::fprintf(stderr, "omachat: settings screenshot requires a connected sandbox account\n");
                        QCoreApplication::exit(1);
                        return;
                    }
                    QTimer::singleShot(client::ThemeProvider::instance()->animationMs() + 150, window, capture);
                } else {
                    capture();
                }
            });
    }
    return app.exec();
}
