#include "application/Daemon.hpp"
#include "omachat/core/Log.hpp"
#include "omachat/core/Paths.hpp"
#include "omachat/core/Version.hpp"
#include "omachat/media/MediaPacket.hpp"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QSocketNotifier>

#include <csignal>
#include <pthread.h>
#include <sys/signalfd.h>
#include <unistd.h>

using namespace omachat;

int main(int argc, char** argv)
{
    std::signal(SIGPIPE, SIG_IGN);
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &mask, nullptr);

    if (::geteuid() == 0) {
        std::fprintf(stderr, "omachatd must not run as root\n");
        return 1;
    }

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("omachatd"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(kVersion));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("OmaChat communications daemon"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(
        {QStringLiteral("socket"), QStringLiteral("IPC socket path."), QStringLiteral("path"), paths::socketPath()});
    parser.addOption({QStringLiteral("database"), QStringLiteral("Local database path."), QStringLiteral("path"),
        paths::databaseFile()});
    parser.addOption(
        {QStringLiteral("config"), QStringLiteral("Config file path."), QStringLiteral("path"), paths::configFile()});
    parser.addOption(
        {QStringLiteral("memory-credentials"), QStringLiteral("Keep session tokens in memory only (no keyring).")});
    parser.addOption({QStringLiteral("null-audio"), QStringLiteral("Disable audio devices (testing/headless).")});
    parser.addOption({QStringLiteral("no-notifications"), QStringLiteral("Do not send desktop notifications.")});
    parser.process(app);

    log::initialize("omachatd", log::Level::Info);
    if (!media::initializeCrypto()) {
        OMA_CRITICAL("daemon", "libsodium initialization failed");
        return 1;
    }

    daemon::DaemonOptions options;
    options.socketPath = parser.value(QStringLiteral("socket"));
    options.databasePath = parser.value(QStringLiteral("database"));
    options.configPath = parser.value(QStringLiteral("config"));
    options.memoryCredentials = parser.isSet(QStringLiteral("memory-credentials"))
        || qEnvironmentVariable("OMACHAT_CREDENTIALS") == u"memory";
    options.nullAudio = parser.isSet(QStringLiteral("null-audio")) || qEnvironmentVariable("OMACHAT_AUDIO") == u"null";
    options.notifications = !parser.isSet(QStringLiteral("no-notifications"));

    daemon::Daemon d(options);
    QString error;
    if (!d.start(&error)) {
        OMA_CRITICAL("daemon", "startup failed", {"error", error});
        return 1;
    }

    const int fd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    QSocketNotifier notifier(fd, QSocketNotifier::Read);
    QObject::connect(&notifier, &QSocketNotifier::activated, &app, [fd, &app, &d] {
        signalfd_siginfo info{};
        while (::read(fd, &info, sizeof info) == sizeof info) { }
        OMA_INFO("daemon", "shutting down");
        d.shutdown();
        app.quit();
    });
    return app.exec();
}
