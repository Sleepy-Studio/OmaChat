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
        : m_username(qgetenv("OMACHAT_OPERATOR_USERNAME"))
        , m_restart(qgetenv("OMACHAT_OPERATOR_REMOTE_RESTART"))
        , m_hadUsername(qEnvironmentVariableIsSet("OMACHAT_OPERATOR_USERNAME"))
        , m_hadRestart(qEnvironmentVariableIsSet("OMACHAT_OPERATOR_REMOTE_RESTART"))
    {
    }
    ~ScopedOperatorEnv()
    {
        restore("OMACHAT_OPERATOR_USERNAME", m_hadUsername, m_username);
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
    QByteArray m_username;
    QByteArray m_restart;
    bool m_hadUsername;
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

TEST(ServerConfigEnv, UsernameOverridesFile)
{
    ScopedOperatorEnv guard;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = writeConfig(dir, QStringLiteral("[operator]\nusername = \"fileuser\"\n"));

    qputenv("OMACHAT_OPERATOR_USERNAME", "envuser");
    server::ServerConfig config;
    QString error;
    ASSERT_TRUE(server::ServerConfig::load(path, config, &error)) << error.toStdString();
    EXPECT_EQ(config.operatorUsername, QStringLiteral("envuser"));

    qputenv("OMACHAT_OPERATOR_USERNAME", "  spaced  ");
    ASSERT_TRUE(server::ServerConfig::load(path, config, &error)) << error.toStdString();
    EXPECT_EQ(config.operatorUsername, QStringLiteral("spaced"));
}

TEST(ServerConfigEnv, EmptyOrUnsetUsernameLeavesFile)
{
    ScopedOperatorEnv guard;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = writeConfig(dir, QStringLiteral("[operator]\nusername = \"fileuser\"\n"));

    qunsetenv("OMACHAT_OPERATOR_USERNAME");
    server::ServerConfig config;
    QString error;
    ASSERT_TRUE(server::ServerConfig::load(path, config, &error)) << error.toStdString();
    EXPECT_EQ(config.operatorUsername, QStringLiteral("fileuser"));

    qputenv("OMACHAT_OPERATOR_USERNAME", "");
    ASSERT_TRUE(server::ServerConfig::load(path, config, &error)) << error.toStdString();
    EXPECT_EQ(config.operatorUsername, QStringLiteral("fileuser"));
}

TEST(ServerConfigEnv, UsernameWorksWithoutFileSection)
{
    ScopedOperatorEnv guard;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = writeConfig(dir, QStringLiteral("[server]\nname = \"Test\"\n"));

    qputenv("OMACHAT_OPERATOR_USERNAME", "envonly");
    server::ServerConfig config;
    QString error;
    ASSERT_TRUE(server::ServerConfig::load(path, config, &error)) << error.toStdString();
    EXPECT_EQ(config.operatorUsername, QStringLiteral("envonly"));
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
