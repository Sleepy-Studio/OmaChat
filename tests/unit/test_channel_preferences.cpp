#include "storage/LocalStore.hpp"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

#include <gtest/gtest.h>

using namespace omachat::daemon;

namespace {
// Independently create the old on-disk schema, rather than rewinding a new store.
class DatabaseFixture {
public:
    explicit DatabaseFixture(const QString& path)
        : name(QUuid::createUuid().toString())
        , db(QSqlDatabase::addDatabase("QSQLITE", name))
    {
        db.setDatabaseName(path);
        EXPECT_TRUE(db.open());
    }
    ~DatabaseFixture()
    {
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }
    void exec(const QString& sql)
    {
        QSqlQuery query(db);
        ASSERT_TRUE(query.exec(sql)) << sql.toStdString();
    }
    qint64 number(const QString& sql)
    {
        QSqlQuery query(db);
        EXPECT_TRUE(query.exec(sql));
        EXPECT_TRUE(query.next());
        return query.value(0).toLongLong();
    }
    void legacy(int version)
    {
        exec("CREATE TABLE accounts(id INTEGER PRIMARY KEY AUTOINCREMENT, host TEXT NOT NULL, port INTEGER NOT NULL, "
             "username TEXT NOT NULL, trusted_fingerprint TEXT NOT NULL DEFAULT '', last_used INTEGER NOT NULL DEFAULT "
             "0, "
             "UNIQUE(host,port,username))");
        exec("CREATE TABLE user_volume(account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE, "
             "user_id INTEGER NOT NULL, gain REAL NOT NULL, PRIMARY KEY(account_id,user_id))");
        exec("CREATE TABLE muted_channels(account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE, "
             "channel_id INTEGER NOT NULL, PRIMARY KEY(account_id,channel_id))");
        exec("CREATE TABLE kv(key TEXT PRIMARY KEY,value TEXT NOT NULL)");
        if (version >= 2)
            exec("CREATE TABLE known_keys(account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE, "
                 "user_id INTEGER NOT NULL,public_key BLOB NOT NULL,verified INTEGER NOT NULL DEFAULT 0, "
                 "PRIMARY KEY(account_id,user_id,public_key))");
        exec("INSERT INTO accounts VALUES(1,'chat.example',6473,'alice','pinned',123)");
        exec("INSERT INTO accounts VALUES(2,'other.example',6473,'bob','other-pin',456)");
        exec("INSERT INTO user_volume VALUES(1,42,1.5)");
        exec("INSERT INTO muted_channels VALUES(1,7)");
        exec("INSERT INTO muted_channels VALUES(2,8)");
        exec("INSERT INTO kv VALUES('theme','dark')");
        exec(QStringLiteral("PRAGMA user_version=%1").arg(version));
    }
    QString name;
    QSqlDatabase db;
};
} // namespace

TEST(ChannelPreferencesStore, LegacyVersionsMigrateAndReopenWithAccountIsolation)
{
    for (int version : {1, 2}) {
        QTemporaryDir dir;
        const auto path = dir.filePath("local.db");
        {
            DatabaseFixture fixture(path);
            fixture.legacy(version);
            if (version == 2)
                fixture.exec("INSERT INTO known_keys VALUES(1,42,x'010203',1)");
        }
        for (int reopen = 0; reopen < 2; ++reopen) {
            LocalStore store;
            QString error;
            ASSERT_TRUE(store.open(path, &error)) << error.toStdString();
            ASSERT_EQ(store.accounts().size(), 2u);
            EXPECT_EQ(store.account(1)->trustedFingerprint, "pinned");
            EXPECT_EQ(store.account(1)->lastUsed, 123);
            EXPECT_EQ(store.account(2)->username, "bob");
            EXPECT_DOUBLE_EQ(store.userVolumes(1).at(42), 1.5);
            EXPECT_EQ(store.value("theme"), "dark");
            EXPECT_EQ(store.channelPreferences(1, 7), (ChannelPreferences{NotificationLevel::Default, 0}));
            EXPECT_EQ(store.channelPreferences(2, 7), ChannelPreferences{});
            EXPECT_EQ(store.mutedChannels(1, 100), (std::set<std::uint64_t>{7}));
            EXPECT_EQ(store.mutedChannels(2, 100), (std::set<std::uint64_t>{8}));
            if (version == 2) {
                EXPECT_EQ(store.knownKeys(1, 42).keys, (std::vector<QByteArray>{QByteArray::fromHex("010203")}));
                EXPECT_TRUE(store.knownKeys(1, 42).verified);
            }
        }
        DatabaseFixture fixture(path);
        EXPECT_EQ(fixture.number("PRAGMA user_version"), 4);
        EXPECT_EQ(fixture.number("SELECT count(*) FROM sqlite_master WHERE name='muted_channels'"), 0);
    }
}

TEST(ChannelPreferencesStore, OverlayPersistsAcrossRestartAndLegacyUnmutePreservesLevel)
{
    QTemporaryDir dir;
    const auto path = dir.filePath("local.db");
    std::int64_t account = 0;
    {
        LocalStore store;
        QString error;
        ASSERT_TRUE(store.open(path, &error));
        account = store.addAccount({0, "chat.example", 6473, "alice", {}, 0});
        ASSERT_GT(account, 0);
        ASSERT_TRUE(store.setChannelPreferences(account, 7, {NotificationLevel::All, 1000}));
        ASSERT_TRUE(store.setChannelPreferences(account, 8, {NotificationLevel::None, {}}));
    }
    LocalStore store;
    QString error;
    ASSERT_TRUE(store.open(path, &error));
    EXPECT_EQ(store.channelPreferences(account, 7), (ChannelPreferences{NotificationLevel::All, 1000}));
    EXPECT_EQ(store.mutedChannels(account, 999), (std::set<std::uint64_t>{7}));
    EXPECT_TRUE(store.mutedChannels(account, 1000).empty());
    EXPECT_TRUE(store.mutedChannels(account, 1001).empty());
    EXPECT_EQ(store.mutedChannels(account, 999), (std::set<std::uint64_t>{7})) << "clock moved back";
    EXPECT_EQ(store.channelPreferences(account, 8), (ChannelPreferences{NotificationLevel::None, {}}));
    ASSERT_TRUE(store.setChannelMuted(account, 7, true));
    EXPECT_EQ(store.channelPreferences(account, 7), (ChannelPreferences{NotificationLevel::All, 0}));
    ASSERT_TRUE(store.setChannelMuted(account, 7, false));
    EXPECT_EQ(store.channelPreferences(account, 7), (ChannelPreferences{NotificationLevel::All, {}}));
    ASSERT_TRUE(store.removeAccount(account));
    ASSERT_TRUE(store.channelPreferences(account));
    EXPECT_TRUE(store.channelPreferences(account)->empty());
}

TEST(ChannelPreferencesStore, FailedMigrationRollsBackSchemaAndVersionAndCanRetry)
{
    QTemporaryDir dir;
    const auto path = dir.filePath("local.db");
    {
        DatabaseFixture fixture(path);
        fixture.legacy(2);
        // Foreign keys are off in the fixture, simulating a damaged legacy database.
        fixture.exec("INSERT INTO muted_channels VALUES(999,9)");
    }
    {
        LocalStore store;
        QString error;
        EXPECT_FALSE(store.open(path, &error));
        EXPECT_FALSE(error.isEmpty());
    }
    {
        DatabaseFixture fixture(path);
        EXPECT_EQ(fixture.number("PRAGMA user_version"), 2);
        EXPECT_EQ(fixture.number("SELECT count(*) FROM muted_channels"), 3);
        EXPECT_EQ(fixture.number("SELECT count(*) FROM sqlite_master WHERE name='channel_preferences'"), 0);
        fixture.exec("DELETE FROM muted_channels WHERE account_id=999");
    }
    LocalStore store;
    QString error;
    ASSERT_TRUE(store.open(path, &error)) << error.toStdString();
    EXPECT_EQ(store.channelPreferences(1, 7), (ChannelPreferences{NotificationLevel::Default, 0}));
}

TEST(ChannelPreferencesStore, FailedEarlyMigrationRollsBackAllEarlierSteps)
{
    QTemporaryDir dir;
    const auto path = dir.filePath("local.db");
    {
        DatabaseFixture fixture(path);
        fixture.legacy(1);
        fixture.exec("INSERT INTO muted_channels VALUES(999,9)");
    }
    {
        LocalStore store;
        QString error;
        EXPECT_FALSE(store.open(path, &error));
    }
    DatabaseFixture fixture(path);
    EXPECT_EQ(fixture.number("PRAGMA user_version"), 1);
    EXPECT_EQ(
        fixture.number("SELECT count(*) FROM sqlite_master WHERE name IN ('known_keys','channel_preferences')"), 0);
    EXPECT_EQ(fixture.number("SELECT count(*) FROM muted_channels"), 3);
}

TEST(ChannelPreferencesStore, RejectsInvalidWritesAndReportsStorageFailure)
{
    QTemporaryDir dir;
    const auto path = dir.filePath("local.db");
    LocalStore store;
    QString error;
    ASSERT_TRUE(store.open(path, &error));
    const auto account = store.addAccount({0, "chat.example", 6473, "alice", {}, 0});
    ASSERT_GT(account, 0);
    ASSERT_TRUE(store.setChannelPreferences(account, 7, {NotificationLevel::Mentions, 1000}));
    EXPECT_FALSE(store.setChannelPreferences(account, 7, {NotificationLevel::All, -1}));
    EXPECT_FALSE(store.setChannelPreferences(account, 7, {static_cast<NotificationLevel>(99), {}}));
    EXPECT_FALSE(store.setChannelPreferences(999, 7, {}));
    EXPECT_EQ(store.channelPreferences(account, 7), (ChannelPreferences{NotificationLevel::Mentions, 1000}));
    DatabaseFixture fixture(path);
    fixture.exec("CREATE TRIGGER fail_preference_write BEFORE UPDATE ON channel_preferences BEGIN SELECT "
                 "RAISE(ABORT,'fixture'); END");
    EXPECT_FALSE(store.setChannelMuted(account, 7, false));
    EXPECT_EQ(store.channelPreferences(account, 7), (ChannelPreferences{NotificationLevel::Mentions, 1000}));
    fixture.exec("DROP TABLE channel_preferences");
    EXPECT_FALSE(store.channelPreferences(account));
    EXPECT_FALSE(store.channelPreferences(account, 7));
    EXPECT_FALSE(store.setChannelMuted(account, 7, true));
}

TEST(ChannelPreferencesStore, RejectsNewerSchemaWithoutChangingIt)
{
    QTemporaryDir dir;
    const auto path = dir.filePath("local.db");
    {
        DatabaseFixture fixture(path);
        fixture.exec("PRAGMA user_version=5");
    }
    {
        LocalStore store;
        QString error;
        EXPECT_FALSE(store.open(path, &error));
        EXPECT_FALSE(error.isEmpty());
    }
    DatabaseFixture fixture(path);
    EXPECT_EQ(fixture.number("PRAGMA user_version"), 5);
    EXPECT_EQ(fixture.number("SELECT count(*) FROM sqlite_master"), 0);
}

TEST(SidebarStore, VersionThreeMigrationPreservesDataAndRollsBackConflictingSchema)
{
    QTemporaryDir dir;
    const auto path = dir.filePath("local.db");
    {
        DatabaseFixture fixture(path);
        fixture.legacy(2);
    }
    {
        LocalStore store;
        QString error;
        ASSERT_TRUE(store.open(path, &error));
        ASSERT_TRUE(store.setChannelPreferences(1, 7, {NotificationLevel::All, 123456}));
    }
    {
        DatabaseFixture fixture(path);
        fixture.exec("DROP TABLE sidebar_channels");
        fixture.exec("DROP TABLE sidebar_views");
        fixture.exec("PRAGMA user_version=3");
        fixture.exec("CREATE TABLE sidebar_views(blocker INTEGER)");
    }
    {
        LocalStore store;
        QString error;
        EXPECT_FALSE(store.open(path, &error));
    }
    {
        DatabaseFixture fixture(path);
        EXPECT_EQ(fixture.number("PRAGMA user_version"), 3);
        EXPECT_EQ(fixture.number("SELECT count(*) FROM sqlite_master WHERE name='sidebar_channels'"), 0);
        fixture.exec("DROP TABLE sidebar_views");
    }
    LocalStore store;
    QString error;
    ASSERT_TRUE(store.open(path, &error)) << error.toStdString();
    EXPECT_EQ(store.channelPreferences(1, 7), (ChannelPreferences{NotificationLevel::All, 123456}));
    EXPECT_EQ(store.account(1)->trustedFingerprint, "pinned");
    EXPECT_DOUBLE_EQ(store.userVolumes(1).at(42), 1.5);
    EXPECT_EQ(store.sidebarPreferences(1), SidebarPreferences{});
}

TEST(SidebarStore, ReopenIsolationCascadeAndTransactionalFailure)
{
    QTemporaryDir dir;
    const auto path = dir.filePath("local.db");
    SidebarPreferences preferences;
    preferences.channels[7] = {0, true, false};
    preferences.channels[8] = {1, false, false};
    preferences.channels[9] = {{}, false, true};
    preferences.compact = true;
    preferences.showVoiceParticipants = false;
    {
        LocalStore store;
        QString error;
        ASSERT_TRUE(store.open(path, &error));
        ASSERT_EQ(store.addAccount({0, "host", 6473, "alice", {}, 0}), 1);
        ASSERT_EQ(store.addAccount({0, "host", 6473, "bob", {}, 0}), 2);
        ASSERT_TRUE(store.setSidebarPreferences(1, preferences));
    }
    LocalStore store;
    QString error;
    ASSERT_TRUE(store.open(path, &error));
    EXPECT_EQ(store.sidebarPreferences(1), preferences);
    EXPECT_EQ(store.sidebarPreferences(2), SidebarPreferences{});
    DatabaseFixture fixture(path);
    fixture.exec("CREATE TRIGGER fail_sidebar BEFORE UPDATE ON sidebar_views BEGIN SELECT RAISE(ABORT,'fixture'); END");
    auto changed = preferences;
    changed.channels.clear();
    EXPECT_FALSE(store.setSidebarPreferences(1, changed));
    EXPECT_EQ(store.sidebarPreferences(1), preferences);
    EXPECT_FALSE(store.setSidebarPreferences(999, preferences));
    fixture.exec("DROP TRIGGER fail_sidebar");
    ASSERT_TRUE(store.setChannelPreferences(1, 7, {NotificationLevel::None, {}}));
    ASSERT_TRUE(store.clearChannelPreferences(1, 7));
    preferences.channels.erase(7);
    EXPECT_EQ(store.sidebarPreferences(1), preferences);
    EXPECT_EQ(store.channelPreferences(1, 7), ChannelPreferences{});
    ASSERT_TRUE(store.removeAccount(1));
    EXPECT_EQ(store.sidebarPreferences(1), SidebarPreferences{});
}
