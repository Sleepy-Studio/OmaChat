#pragma once

#include "Harness.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QMap>

namespace omachat::test {

struct RecoveryKeyringState {
    QMap<QString, QString> secrets;
    bool failRecoveryRead = false;
    bool failRecoveryWrite = false;
    bool failRecoveryRemove = false;
    int recoveryReadCount = 0;
    int failRecoveryReadOn = 0;
};

// Explicit in-process secure-boundary double. It is deliberately unavailable
// through production environment/configuration and does not test a real keyring.
class RecoveryKeyring final : public daemon::ICredentialStore {
public:
    explicit RecoveryKeyring(std::shared_ptr<RecoveryKeyringState> state)
        : m_state(std::move(state))
    {
    }
    QString backendName() const override { return QStringLiteral("keyring"); }
    void read(const QString& key, ReadCallback cb) override
    {
        const bool recovery = key.startsWith(QStringLiteral("send-recovery-v1/"));
        if (recovery)
            ++m_state->recoveryReadCount;
        const bool fail = recovery
            && (m_state->failRecoveryRead
                || (m_state->failRecoveryReadOn > 0 && m_state->recoveryReadCount == m_state->failRecoveryReadOn));
        cb(!fail, fail ? QString() : m_state->secrets.value(key),
            fail ? QStringLiteral("fixture keyring locked") : QString());
    }
    void write(const QString& key, const QString& value, WriteCallback cb) override
    {
        const bool fail = key.startsWith(QStringLiteral("send-recovery-v1/")) && m_state->failRecoveryWrite;
        if (!fail)
            m_state->secrets[key] = value;
        if (cb)
            cb(!fail, fail ? QStringLiteral("fixture keyring write denied") : QString());
    }
    void remove(const QString& key, WriteCallback cb) override
    {
        const bool fail = key.startsWith(QStringLiteral("send-recovery-v1/")) && m_state->failRecoveryRemove;
        if (!fail)
            m_state->secrets.remove(key);
        if (cb)
            cb(!fail, fail ? QStringLiteral("fixture keyring delete denied") : QString());
    }

private:
    std::shared_ptr<RecoveryKeyringState> m_state;
};

// Stable root/keyring across restart, real daemon IPC and real server. Caller
// owns the temporary directory. GUI tests may connect another IPC client.
class RecoveryDaemon {
public:
    explicit RecoveryDaemon(
        QString root, std::shared_ptr<RecoveryKeyringState> keys = std::make_shared<RecoveryKeyringState>())
        : directory(std::move(root))
        , keyring(std::move(keys))
        , socketPath(directory + QStringLiteral("/daemon.sock"))
    {
    }
    ~RecoveryDaemon() { stop(); }
    bool start()
    {
        daemon::DaemonOptions options;
        options.databasePath = directory + QStringLiteral("/local.db");
        options.configPath = directory + QStringLiteral("/config.toml");
        options.socketPath = socketPath;
        options.nullAudio = true;
        options.syntheticScreen = true;
        options.notifications = false;
        options.credentialStoreFactory = [keys = keyring](QObject*) { return std::make_unique<RecoveryKeyring>(keys); };
        daemon = std::make_unique<daemon::Daemon>(options);
        QString error;
        if (!daemon->start(&error))
            return false;
        ipc.connectToDaemon(socketPath);
        return waitFor([&] { return ipc.isConnected(); });
    }
    void stop()
    {
        ipc.disconnectFromDaemon();
        if (daemon)
            daemon->shutdown();
        daemon.reset();
    }
    ipc::Reply call(const QString& method, const QJsonObject& params = {}, int timeout = 15000)
    {
        std::optional<ipc::Reply> reply;
        ipc.request(method, params, [&](const ipc::Reply& r) { reply = r; }, timeout);
        if (!waitFor([&] { return reply.has_value(); }, timeout + 500))
            return {false, {}, QStringLiteral("Timeout"), QStringLiteral("fixture IPC request timed out")};
        return *reply;
    }
    bool connected()
    {
        return waitFor(
            [&] { return daemon->statusJson().value("state").toString() == QStringLiteral("connected"); }, 15000);
    }
    bool authenticate(const TestServer& server, QString user, bool registering = true)
    {
        const auto method = registering ? QStringLiteral("account.register") : QStringLiteral("account.login");
        const QJsonObject params{{"host", "127.0.0.1"}, {"port", server.port()}, {"username", user},
            {"password", "recovery-test-password"}, {"display_name", user}};
        auto reply = call(method, params, 20000);
        if (!reply.ok && reply.errorCode == QStringLiteral("CertificateError")) {
            if (!call(QStringLiteral("certificate.trust"), {{"fingerprint", server.fingerprint()}}).ok)
                return false;
            if (!waitFor(
                    [&] { return daemon->statusJson().value("state").toString() == QStringLiteral("login_required"); }))
                return false;
            reply = call(method, params, 20000);
        }
        return reply.ok && connected();
    }
    daemon::SendRecoveryStore::Binding binding() const
    {
        const auto& account = daemon->connection().account();
        return {QString::number(account.id),
            QStringLiteral("tls://%1:%2").arg(account.host.toLower()).arg(account.port), account.trustedFingerprint,
            QString::number(daemon->connection().model().self().id())};
    }
    QString directory;
    std::shared_ptr<RecoveryKeyringState> keyring;
    QString socketPath;
    std::unique_ptr<daemon::Daemon> daemon;
    ipc::IpcClient ipc;
};

} // namespace omachat::test
