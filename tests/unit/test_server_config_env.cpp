#include "config/ServerConfig.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QtGlobal>

#include <array>
#include <gtest/gtest.h>
#include <optional>
#include <vector>

using namespace omachat;

namespace {

// Saves the operator env vars on construction, restores them after the test
// so process-global state never leaks between cases.
class ScopedOperatorEnv {
public:
    ScopedOperatorEnv()
        : m_userId(qgetenv("OMACHAT_OPERATOR_USER_ID"))
        , m_restart(qgetenv("OMACHAT_OPERATOR_REMOTE_RESTART"))
        , m_hadUserId(qEnvironmentVariableIsSet("OMACHAT_OPERATOR_USER_ID"))
        , m_hadRestart(qEnvironmentVariableIsSet("OMACHAT_OPERATOR_REMOTE_RESTART"))
    {
    }
    ~ScopedOperatorEnv()
    {
        restore("OMACHAT_OPERATOR_USER_ID", m_hadUserId, m_userId);
        restore("OMACHAT_OPERATOR_REMOTE_RESTART", m_hadRestart, m_restart);
    }

private:
    static void restore(const char* name, bool had, const QByteArray& value)
    {
        if (had)
            qputenv(name, value);
        else
            qunsetenv(name);
    }
    QByteArray m_userId;
    QByteArray m_restart;
    bool m_hadUserId;
    bool m_hadRestart;
};

QString writeConfig(QTemporaryDir& dir, const QString& body)
{
    const QString path = dir.filePath(QStringLiteral("server.toml"));
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(body.toUtf8());
    file.close();
    return path;
}

} // namespace

TEST(ServerConfigEnv, UserIdFromFile)
{
    ScopedOperatorEnv guard;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = writeConfig(dir, QStringLiteral("[operator]\nuser_id = 12345\n"));

    qunsetenv("OMACHAT_OPERATOR_USER_ID");
    server::ServerConfig config;
    QString error;
    ASSERT_TRUE(server::ServerConfig::load(path, config, &error)) << error.toStdString();
    EXPECT_EQ(config.operatorUserId, 12345u);
}

TEST(ServerConfigEnv, UserIdEnvOverridesFile)
{
    ScopedOperatorEnv guard;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = writeConfig(dir, QStringLiteral("[operator]\nuser_id = 12345\n"));

    qputenv("OMACHAT_OPERATOR_USER_ID", "67890");
    server::ServerConfig config;
    QString error;
    ASSERT_TRUE(server::ServerConfig::load(path, config, &error)) << error.toStdString();
    EXPECT_EQ(config.operatorUserId, 67890u);
}

TEST(ServerConfigEnv, EmptyUserIdEnvLeavesFile)
{
    ScopedOperatorEnv guard;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = writeConfig(dir, QStringLiteral("[operator]\nuser_id = 12345\n"));

    qputenv("OMACHAT_OPERATOR_USER_ID", "");
    server::ServerConfig config;
    QString error;
    ASSERT_TRUE(server::ServerConfig::load(path, config, &error)) << error.toStdString();
    EXPECT_EQ(config.operatorUserId, 12345u);
}

TEST(ServerConfigEnv, UserIdWorksWithoutFileSection)
{
    ScopedOperatorEnv guard;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = writeConfig(dir, QStringLiteral("[server]\nname = \"Test\"\n"));

    qputenv("OMACHAT_OPERATOR_USER_ID", "67890");
    server::ServerConfig config;
    QString error;
    ASSERT_TRUE(server::ServerConfig::load(path, config, &error)) << error.toStdString();
    EXPECT_EQ(config.operatorUserId, 67890u);
}

TEST(ServerConfigEnv, InvalidUserIdFailsLoad)
{
    ScopedOperatorEnv guard;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = writeConfig(dir, QStringLiteral("[server]\nname = \"Test\"\n"));

    for (const char* bad : {"alice", "-5", "0", "12.5", "99999999999999999999999999"}) {
        qputenv("OMACHAT_OPERATOR_USER_ID", bad);
        server::ServerConfig config;
        QString error;
        EXPECT_FALSE(server::ServerConfig::load(path, config, &error)) << bad;
        EXPECT_TRUE(error.contains(QStringLiteral("OMACHAT_OPERATOR_USER_ID"))) << bad;
    }
}

TEST(ServerConfigEnv, InvalidFileUserIdFailsLoad)
{
    ScopedOperatorEnv guard;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    qunsetenv("OMACHAT_OPERATOR_USER_ID");

    const QString path = writeConfig(dir, QStringLiteral("[operator]\nuser_id = 0\n"));
    server::ServerConfig config;
    QString error;
    EXPECT_FALSE(server::ServerConfig::load(path, config, &error));
}

TEST(ServerConfigEnv, RemoteRestartParsesBooleans)
{
    ScopedOperatorEnv guard;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = writeConfig(dir, QStringLiteral("[server]\nname = \"Test\"\n"));

    for (const char* truthy : {"1", "true", "True", "YES", "on"}) {
        qputenv("OMACHAT_OPERATOR_REMOTE_RESTART", truthy);
        server::ServerConfig config;
        QString error;
        ASSERT_TRUE(server::ServerConfig::load(path, config, &error)) << error.toStdString();
        EXPECT_TRUE(config.remoteRestart) << truthy;
    }
    for (const char* falsy : {"0", "false", "False", "NO", "off"}) {
        qputenv("OMACHAT_OPERATOR_REMOTE_RESTART", falsy);
        server::ServerConfig config;
        QString error;
        ASSERT_TRUE(server::ServerConfig::load(path, config, &error)) << error.toStdString();
        EXPECT_FALSE(config.remoteRestart) << falsy;
    }
}

TEST(ServerConfigEnv, InvalidRemoteRestartFailsLoad)
{
    ScopedOperatorEnv guard;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = writeConfig(dir, QStringLiteral("[server]\nname = \"Test\"\n"));

    qputenv("OMACHAT_OPERATOR_REMOTE_RESTART", "maybe");
    server::ServerConfig config;
    QString error;
    EXPECT_FALSE(server::ServerConfig::load(path, config, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("OMACHAT_OPERATOR_REMOTE_RESTART")));
}

namespace {
class ScopedWebhookEnv {
public:
    ScopedWebhookEnv()
    {
        for (const char* name : names) {
            values.push_back(qEnvironmentVariableIsSet(name) ? std::optional<QByteArray>(qgetenv(name)) : std::nullopt);
            qunsetenv(name);
        }
    }
    ~ScopedWebhookEnv()
    {
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (values[i])
                qputenv(names[i], *values[i]);
            else
                qunsetenv(names[i]);
        }
    }

private:
    const std::array<const char*, 3> names{
        "OMACHAT_WEBHOOKS_ENABLED", "OMACHAT_WEBHOOKS_BIND", "OMACHAT_WEBHOOKS_PORT"};
    std::vector<std::optional<QByteArray>> values;
};
} // namespace
TEST(WebhookConfigEnv, SafeDefaultsAndTomlConfiguration)
{
    ScopedWebhookEnv guard;
    QTemporaryDir dir;
    QString error;
    auto path = writeConfig(dir, QStringLiteral("[server]\nname='Defaults'\n"));
    server::ServerConfig defaults;
    ASSERT_TRUE(server::ServerConfig::load(path, defaults, &error)) << error.toStdString();
    EXPECT_FALSE(defaults.webhooksEnabled);
    EXPECT_EQ(defaults.webhooksBind, "127.0.0.1");
    EXPECT_EQ(defaults.webhooksPort, 6475);
    path = writeConfig(dir, QStringLiteral("[webhooks]\nenabled=true\nbind='::1'\nport=7445\n"));
    server::ServerConfig configured;
    ASSERT_TRUE(server::ServerConfig::load(path, configured, &error)) << error.toStdString();
    EXPECT_TRUE(configured.webhooksEnabled);
    EXPECT_EQ(configured.webhooksBind, "::1");
    EXPECT_EQ(configured.webhooksPort, 7445);
}
TEST(WebhookConfigEnv, EnvironmentOverridesAndEmptyPreservesToml)
{
    ScopedWebhookEnv guard;
    QTemporaryDir dir;
    QString error;
    auto path = writeConfig(dir, QStringLiteral("[webhooks]\nenabled=true\nbind='::1'\nport=7445\n"));
    qputenv("OMACHAT_WEBHOOKS_ENABLED", "false");
    qputenv("OMACHAT_WEBHOOKS_BIND", "127.0.0.2");
    qputenv("OMACHAT_WEBHOOKS_PORT", "65535");
    server::ServerConfig configured;
    ASSERT_TRUE(server::ServerConfig::load(path, configured, &error)) << error.toStdString();
    EXPECT_FALSE(configured.webhooksEnabled);
    EXPECT_EQ(configured.webhooksBind, "127.0.0.2");
    EXPECT_EQ(configured.webhooksPort, 65535);
    qputenv("OMACHAT_WEBHOOKS_ENABLED", "");
    qputenv("OMACHAT_WEBHOOKS_BIND", "");
    qputenv("OMACHAT_WEBHOOKS_PORT", "");
    server::ServerConfig empty;
    ASSERT_TRUE(server::ServerConfig::load(path, empty, &error)) << error.toStdString();
    EXPECT_TRUE(empty.webhooksEnabled);
    EXPECT_EQ(empty.webhooksBind, "::1");
    EXPECT_EQ(empty.webhooksPort, 7445);
}
TEST(WebhookConfigEnv, BooleanSpellingsAndInvalidBoolean)
{
    ScopedWebhookEnv guard;
    QTemporaryDir dir;
    QString error;
    auto path = writeConfig(dir, QStringLiteral("[server]\nname='Flags'\n"));
    for (const char* value : {"1", "true", "YES", "on"}) {
        qputenv("OMACHAT_WEBHOOKS_ENABLED", value);
        server::ServerConfig configured;
        ASSERT_TRUE(server::ServerConfig::load(path, configured, &error));
        EXPECT_TRUE(configured.webhooksEnabled);
    }
    for (const char* value : {"0", "false", "NO", "off"}) {
        qputenv("OMACHAT_WEBHOOKS_ENABLED", value);
        server::ServerConfig configured;
        ASSERT_TRUE(server::ServerConfig::load(path, configured, &error));
        EXPECT_FALSE(configured.webhooksEnabled);
    }
    qputenv("OMACHAT_WEBHOOKS_ENABLED", "sometimes");
    server::ServerConfig configured;
    EXPECT_FALSE(server::ServerConfig::load(path, configured, &error));
    EXPECT_TRUE(error.contains("OMACHAT_WEBHOOKS_ENABLED"));
}
TEST(WebhookConfigEnv, InvalidPortsFailWithoutSilentFallback)
{
    ScopedWebhookEnv guard;
    QTemporaryDir dir;
    auto path = writeConfig(dir, QStringLiteral("[webhooks]\nport=6475\n"));
    for (const char* value : {"0", "65536", "-1", "+80", "1.5", "http", "99999999999999999999"}) {
        qputenv("OMACHAT_WEBHOOKS_PORT", value);
        server::ServerConfig configured;
        QString error;
        EXPECT_FALSE(server::ServerConfig::load(path, configured, &error)) << value;
        EXPECT_TRUE(error.contains("OMACHAT_WEBHOOKS_PORT")) << value;
    }
}
TEST(WebhookConfigEnv, InvalidBindNeverFallsBackToWildcard)
{
    ScopedWebhookEnv guard;
    QTemporaryDir dir;
    auto path = writeConfig(dir, QStringLiteral("[webhooks]\nbind='127.0.0.1'\n"));
    for (const char* value : {"localhost", "https://127.0.0.1", "not-an-ip", "127.0.0.1:6475"}) {
        qputenv("OMACHAT_WEBHOOKS_BIND", value);
        server::ServerConfig configured;
        QString error;
        EXPECT_FALSE(server::ServerConfig::load(path, configured, &error)) << value;
        EXPECT_TRUE(error.contains("OMACHAT_WEBHOOKS_BIND")) << value;
    }
    qputenv("OMACHAT_WEBHOOKS_BIND", "0.0.0.0");
    server::ServerConfig configured;
    QString error;
    ASSERT_TRUE(server::ServerConfig::load(path, configured, &error));
    EXPECT_EQ(configured.webhooksBind, "0.0.0.0");
}
TEST(WebhookConfigEnv, InvalidTomlTypesAndValuesRejectEvenWhenDisabled)
{
    ScopedWebhookEnv guard;
    QTemporaryDir dir;
    for (const auto& body : {"enabled='yes'", "bind=123", "bind='localhost'", "port=0", "port=65536", "port='http'"}) {
        const auto path = writeConfig(dir, QStringLiteral("[webhooks]\n") + QString::fromLatin1(body) + u'\n');
        server::ServerConfig configured;
        QString error;
        EXPECT_FALSE(server::ServerConfig::load(path, configured, &error)) << body;
        EXPECT_TRUE(error.contains("webhooks.")) << body;
    }
}
