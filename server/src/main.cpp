#include "config/ServerConfig.hpp"
#include "core/ChatServer.hpp"
#include "omachat/core/Log.hpp"
#include "omachat/core/Version.hpp"
#include "omachat/media/MediaPacket.hpp"
#include "transport/Certificates.hpp"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QSocketNotifier>

#include <csignal>
#include <cstdio>
#include <pthread.h>

#include <sys/signalfd.h>
#include <unistd.h>

using namespace omachat;

namespace {

int generateCert(const QStringList& args)
{
    QCommandLineParser p;
    p.setApplicationDescription(QStringLiteral("Generate a self-signed TLS certificate for omachat-server"));
    p.addHelpOption();
    p.addOption({QStringLiteral("cert"), QStringLiteral("Certificate output path (PEM)."), QStringLiteral("path")});
    p.addOption(
        {QStringLiteral("key"), QStringLiteral("Private key output path (PEM, mode 0600)."), QStringLiteral("path")});
    p.addOption({QStringLiteral("name"), QStringLiteral("DNS name or IP for subjectAltName (repeatable)."),
        QStringLiteral("host")});
    p.addOption({QStringLiteral("days"), QStringLiteral("Validity in days (default 825)."), QStringLiteral("n"),
        QStringLiteral("825")});
    p.process(args);
    if (!p.isSet(QStringLiteral("cert")) || !p.isSet(QStringLiteral("key"))) {
        std::fprintf(stderr, "generate-cert: --cert and --key are required\n");
        return 2;
    }
    QStringList names = p.values(QStringLiteral("name"));
    if (names.isEmpty())
        names << QStringLiteral("localhost") << QStringLiteral("127.0.0.1") << QStringLiteral("::1");
    QString error;
    if (!server::generateSelfSigned(p.value(QStringLiteral("cert")), p.value(QStringLiteral("key")), names,
            p.value(QStringLiteral("days")).toInt(), &error)) {
        std::fprintf(stderr, "generate-cert: %s\n", qPrintable(error));
        return 1;
    }
    server::TlsIdentity id;
    server::loadTlsIdentity(p.value(QStringLiteral("cert")), p.value(QStringLiteral("key")), id, &error);
    std::printf("certificate: %s\nprivate key: %s\nfingerprint: %s\n", qPrintable(p.value(QStringLiteral("cert"))),
        qPrintable(p.value(QStringLiteral("key"))),
        id.chain.isEmpty() ? "?" : qPrintable(server::fingerprint(id.chain.first())));
    return 0;
}

// Routes SIGINT/SIGTERM through the event loop via signalfd so shutdown runs
// on the main thread instead of inside a signal handler.
sigset_t shutdownSignals()
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    return mask;
}

void installSignalHandling(QCoreApplication& app)
{
    const sigset_t mask = shutdownSignals();
    const int fd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    if (fd < 0)
        return;
    auto* notifier = new QSocketNotifier(fd, QSocketNotifier::Read, &app);
    QObject::connect(notifier, &QSocketNotifier::activated, &app, [fd, &app] {
        signalfd_siginfo info{};
        while (::read(fd, &info, sizeof info) == sizeof info) { }
        OMA_INFO("server", "shutting down");
        app.quit();
    });
}

} // namespace

int main(int argc, char** argv)
{
    // Never allow a fatal SIGPIPE from a peer that vanished mid-write.
    std::signal(SIGPIPE, SIG_IGN);
    // Block shutdown signals before any thread exists so every thread
    // inherits the mask and they are only ever consumed via signalfd.
    const sigset_t mask = shutdownSignals();
    pthread_sigmask(SIG_BLOCK, &mask, nullptr);

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("omachat-server"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(kVersion));

    const QStringList args = app.arguments();
    if (args.size() > 1 && args.at(1) == u"generate-cert") {
        QStringList sub = args;
        sub.removeAt(1);
        return generateCert(sub);
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Self-hostable OmaChat server.\n\nSubcommands:\n  generate-cert   create a self-signed TLS certificate"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({{QStringLiteral("c"), QStringLiteral("config")}, QStringLiteral("Path to server.toml."),
        QStringLiteral("path"), QStringLiteral("/etc/omachat/server.toml")});
    parser.process(app);

    log::initialize("omachat-server", log::Level::Info);
    if (!media::initializeCrypto()) {
        OMA_CRITICAL("server", "libsodium initialization failed");
        return 1;
    }

    const QString configPath = parser.value(QStringLiteral("config"));
    server::ServerConfig config;
    QString error;
    if (!QFileInfo::exists(configPath)) {
        OMA_CRITICAL("server", "configuration file not found", {"path", configPath},
            {"hint", "see docs/self-hosting.md or packaging/server/server.toml.example"});
        return 1;
    }
    if (!server::ServerConfig::load(configPath, config, &error)) {
        OMA_CRITICAL("server", "invalid configuration", {"path", configPath}, {"error", error});
        return 1;
    }
    if (auto lvl = log::parseLevel(config.logLevel.toStdString());
        lvl && !qEnvironmentVariableIsSet("OMACHAT_LOG_LEVEL"))
        log::setLevel(*lvl);

    if (config.tlsCertificate.isEmpty() || config.tlsPrivateKey.isEmpty()) {
        OMA_CRITICAL("server", "tls.certificate and tls.private_key are required",
            {"hint", "omachat-server generate-cert --cert cert.pem --key key.pem --name your.host"});
        return 1;
    }
    server::TlsIdentity identity;
    if (!server::loadTlsIdentity(config.tlsCertificate, config.tlsPrivateKey, identity, &error)) {
        OMA_CRITICAL("server", "cannot load TLS identity", {"error", error});
        return 1;
    }
    OMA_INFO("server", "tls identity loaded", {"fingerprint", server::fingerprint(identity.chain.first())},
        {"expires", identity.chain.first().expiryDate().toString(Qt::ISODate)});

    server::ChatServer chat(config);
    if (!chat.start(identity, &error)) {
        OMA_CRITICAL("server", "startup failed", {"error", error});
        return 1;
    }
    installSignalHandling(app);
    return app.exec();
}
