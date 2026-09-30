#include "config/ServerConfig.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QtGlobal>

#include <gtest/gtest.h>

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
