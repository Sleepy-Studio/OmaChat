#include "storage/Store.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <gtest/gtest.h>
#include <unistd.h>

using namespace omachat;

namespace {
class SqlConnection {
public:
    explicit SqlConnection(const QString& path, bool readOnly = false)
        : name(QUuid::createUuid().toString())
        , db(QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name))
    {
        if (readOnly)
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        db.setDatabaseName(path);
        opened = db.open();
    }
    ~SqlConnection()
    {
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }
    QString name;
    QSqlDatabase db;
    bool opened = false;
};

bool fixture(const QString& path)
{
    server::Store store;
    QString error;
    if (!store.open(path, &error) || !store.insertUser({1, "backup-user", "Backup User", {}, "fixture-hash", 100}))
        return false;
    server::ServerRecord community;
    community.id = 20;
    community.name = "Backup lab";
    community.ownerId = 1;
    if (!store.insertServer(community, 100)
        || !store.insertChannel({30, 20, "general", server::ChannelKind::Text, 0, 0, {}, {}}, 100))
        return false;
    server::MessageRecord message;
    message.id = 40;
    message.channelId = 30;
    message.authorId = 1;
    message.content = "Preserve this real row";
    message.createdAt = 100;
    return store.insertMessage(message);
}

bool rewind(const QString& path, int version)
{
    SqlConnection connection(path);
    if (!connection.opened)
        return false;
    QSqlQuery query(connection.db);
    const QStringList statements{"DROP TABLE incoming_webhooks", "ALTER TABLE messages DROP COLUMN integration_id",
        "ALTER TABLE messages DROP COLUMN integration_name"};
    for (const auto& statement : statements)
        if (!query.exec(statement))
            return false;
    if (version == 15
        && (!query.exec("DROP TABLE message_attention") || !query.exec("DROP TABLE message_classification")))
        return false;
    return query.exec(QStringLiteral("PRAGMA user_version=%1").arg(version));
}

void expectBackup(const QString& path, int version)
{
    const auto permissions = QFileInfo(path).permissions();
    EXPECT_TRUE(permissions.testFlag(QFileDevice::ReadOwner));
    EXPECT_TRUE(permissions.testFlag(QFileDevice::WriteOwner));
    EXPECT_FALSE(permissions
        & (QFileDevice::ExeOwner | QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
            | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther));
    SqlConnection check(path, true);
    ASSERT_TRUE(check.opened);
    QSqlQuery query(check.db);
    ASSERT_TRUE(query.exec("PRAGMA integrity_check"));
    ASSERT_TRUE(query.next());
    EXPECT_EQ(query.value(0).toString(), "ok");
    EXPECT_FALSE(query.next());
    ASSERT_TRUE(query.exec("PRAGMA user_version"));
    ASSERT_TRUE(query.next());
    EXPECT_EQ(query.value(0).toInt(), version);
    ASSERT_TRUE(query.exec("SELECT content FROM messages WHERE id=40"));
    ASSERT_TRUE(query.next());
    EXPECT_EQ(query.value(0).toString(), "Preserve this real row");
    ASSERT_TRUE(query.exec("SELECT username FROM users WHERE id=1"));
    ASSERT_TRUE(query.next());
    EXPECT_EQ(query.value(0).toString(), "backup-user");
}
} // namespace

TEST(MigrationBackup, Schema15SnapshotIsPrivateVerifiedAndNeverOverwritten)
{
    QTemporaryDir directory;
    const auto path = directory.filePath("source' quoted.db"); // bound VACUUM destination
    ASSERT_TRUE(fixture(path));
    ASSERT_TRUE(rewind(path, 15));
    QString originalBackup;
    {
        server::Store store;
        QString error;
        ASSERT_TRUE(store.open(path, &error)) << error.toStdString();
        EXPECT_EQ(store.schemaVersion(), 17);
        ASSERT_TRUE(store.message(40));
        EXPECT_EQ(store.message(40)->content, "Preserve this real row");
        originalBackup = store.migrationBackupPath();
        ASSERT_FALSE(originalBackup.isEmpty());
        expectBackup(originalBackup, 15);
    }
    ASSERT_TRUE(rewind(path, 15));
    {
        server::Store store;
        QString error;
        ASSERT_TRUE(store.open(path, &error)) << error.toStdString();
        EXPECT_NE(store.migrationBackupPath(), originalBackup);
        expectBackup(store.migrationBackupPath(), 15);
        expectBackup(originalBackup, 15);
    }
}

TEST(MigrationBackup, Schema16BackupAndFreshCurrentMemorySkips)
{
    QTemporaryDir directory;
    const auto path = directory.filePath("source.db");
    ASSERT_TRUE(fixture(path));
    ASSERT_TRUE(rewind(path, 16));
    {
        server::Store store;
        QString error;
        ASSERT_TRUE(store.open(path, &error)) << error.toStdString();
        expectBackup(store.migrationBackupPath(), 16);
    }
    const auto files = QDir(directory.path()).entryList(QDir::Files);
    {
        server::Store current;
        QString error;
        ASSERT_TRUE(current.open(path, &error));
        EXPECT_TRUE(current.migrationBackupPath().isEmpty());
    }
    EXPECT_EQ(QDir(directory.path()).entryList(QDir::Files), files);
    {
        server::Store fresh;
        QString error;
        ASSERT_TRUE(fresh.open(directory.filePath("fresh.db"), &error));
        EXPECT_TRUE(fresh.migrationBackupPath().isEmpty());
        server::Store memory;
        ASSERT_TRUE(memory.open(":memory:", &error));
        EXPECT_TRUE(memory.migrationBackupPath().isEmpty());
    }
}

TEST(MigrationBackup, UnwritableBackupParentFailsClosedBeforeSchemaChanges)
{
    if (::geteuid() == 0)
        GTEST_SKIP() << "requires ordinary-user filesystem permission enforcement";
    QTemporaryDir directory;
    const auto path = directory.filePath("source.db");
    ASSERT_TRUE(fixture(path));
    ASSERT_TRUE(rewind(path, 15));
    SqlConnection keeper(path);
    ASSERT_TRUE(keeper.opened);
    {
        QSqlQuery query(keeper.db);
        ASSERT_TRUE(query.exec("PRAGMA journal_mode=WAL"));
        ASSERT_TRUE(query.exec("PRAGMA user_version=15")); // keep writable WAL/SHM files alive
    }
    const auto originalPermissions = QFileInfo(directory.path()).permissions();
    const auto restore = qScopeGuard([&] { QFile::setPermissions(directory.path(), originalPermissions); });
    ASSERT_TRUE(QFile::setPermissions(directory.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    {
        server::Store store;
        QString error;
        EXPECT_FALSE(store.open(path, &error));
        EXPECT_TRUE(error.contains("backup"));
        EXPECT_TRUE(store.migrationBackupPath().isEmpty());
        EXPECT_EQ(store.schemaVersion(), 15);
    }
    ASSERT_TRUE(QFile::setPermissions(directory.path(), originalPermissions));
    QSqlQuery query(keeper.db);
    ASSERT_TRUE(query.exec("PRAGMA user_version"));
    ASSERT_TRUE(query.next());
    EXPECT_EQ(query.value(0).toInt(), 15);
    ASSERT_TRUE(query.exec("SELECT content FROM messages WHERE id=40"));
    ASSERT_TRUE(query.next());
    EXPECT_EQ(query.value(0).toString(), "Preserve this real row");
    EXPECT_TRUE(QDir(directory.path()).entryList({"*.pre-migration-*"}, QDir::Files).isEmpty());
}

TEST(MigrationBackup, VerifiedBackupSurvivesLaterMigrationFailure)
{
    QTemporaryDir directory;
    const auto path = directory.filePath("source.db");
    ASSERT_TRUE(fixture(path));
    ASSERT_TRUE(rewind(path, 15));
    {
        SqlConnection source(path);
        ASSERT_TRUE(source.opened);
        QSqlQuery query(source.db);
        ASSERT_TRUE(query.exec("CREATE TABLE incoming_webhooks(collision INTEGER)"));
    }
    server::Store store;
    QString error;
    EXPECT_FALSE(store.open(path, &error));
    EXPECT_EQ(store.schemaVersion(), 15);
    ASSERT_FALSE(store.migrationBackupPath().isEmpty());
    expectBackup(store.migrationBackupPath(), 15);
}
