#include "storage/LocalStore.hpp"
#include "storage/Store.hpp"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace omachat;

TEST(ServerStore, UsersSessionsAndConflicts)
{
    QTemporaryDir dir;
    server::Store store;
    QString error;
    ASSERT_TRUE(store.open(dir.filePath(QStringLiteral("s.db")), &error)) << error.toStdString();
    EXPECT_EQ(store.schemaVersion(), 5);
    server::UserRecord u{
        1, QStringLiteral("alice"), QStringLiteral("Alice"), QString(), QStringLiteral("$argon2id$x"), 5};
    EXPECT_TRUE(store.insertUser(u)) << "null avatar must be stored as empty string";
    EXPECT_FALSE(store.insertUser(u)) << "duplicate username rejected";
    EXPECT_EQ(store.userByName(QStringLiteral("alice"))->displayName, QStringLiteral("Alice"));
    EXPECT_TRUE(store.updateUserProfile(1, QStringLiteral("Alice Two"), QStringLiteral("https://example.org/a.png"),
        QStringLiteral("A short bio")));
    const auto profile = store.userByName(QStringLiteral("alice"));
    ASSERT_TRUE(profile.has_value());
    EXPECT_EQ(profile->displayName, QStringLiteral("Alice Two"));
    EXPECT_EQ(profile->avatarUrl, QStringLiteral("https://example.org/a.png"));
    EXPECT_EQ(profile->bio, QStringLiteral("A short bio"));

    server::SessionRecord s{7, 1, QByteArray(32, 'k'), 1000};
    EXPECT_TRUE(store.insertSession(s));
    EXPECT_EQ(store.sessionByDigest(QByteArray(32, 'k'))->userId, 1u);
    EXPECT_TRUE(store.rotateSession(7, QByteArray(32, 'n'), 2000));
    EXPECT_FALSE(store.sessionByDigest(QByteArray(32, 'k')).has_value()) << "old refresh token is dead";
    EXPECT_EQ(store.purgeExpiredSessions(5000), 1);
}

TEST(ServerStore, VersionOneDatabasesAreMigrated)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("s.db"));
    {
        server::Store store;
        QString error;
        ASSERT_TRUE(store.open(path, &error));
        ASSERT_TRUE(store.insertUser({1, QStringLiteral("a"), QStringLiteral("A"), {}, QStringLiteral("h"), 0}));
    }
    {
        // Rewind to what a 0.1.0 server left on disk.
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("rewind"));
        db.setDatabaseName(path);
        ASSERT_TRUE(db.open());
        QSqlQuery q(db);
        ASSERT_TRUE(q.exec(QStringLiteral("DROP TABLE attachments")));
        ASSERT_TRUE(q.exec(QStringLiteral("DROP TABLE device_keys")));
        ASSERT_TRUE(q.exec(QStringLiteral("DROP TABLE oauth_identities")));
        ASSERT_TRUE(q.exec(QStringLiteral("ALTER TABLE users DROP COLUMN bio")));
        ASSERT_TRUE(q.exec(QStringLiteral("ALTER TABLE messages DROP COLUMN encrypted")));
        ASSERT_TRUE(q.exec(QStringLiteral("PRAGMA user_version=1")));
        q.finish();
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("rewind"));

    server::Store store;
    QString error;
    ASSERT_TRUE(store.open(path, &error)) << error.toStdString();
    EXPECT_EQ(store.schemaVersion(), 5);
    EXPECT_TRUE(store.userByName(QStringLiteral("a")).has_value()) << "existing data survives";
    EXPECT_EQ(store.pendingAttachmentCount(1), 0);

    // v3: encrypted payloads and device keys.
    server::ServerRecord srv;
    srv.id = 10;
    srv.name = QStringLiteral("S");
    srv.ownerId = 1;
    ASSERT_TRUE(store.insertServer(srv, 0));
    ASSERT_TRUE(store.insertChannel({20, 10, QStringLiteral("general"), server::ChannelKind::Text, 0, 0, {}, {}}, 0));
    server::MessageRecord m{30, 20, 1, QString(), 0, 0, false, {}, {}, QByteArray("\x01sealed", 7)};
    ASSERT_TRUE(store.insertMessage(m));
    EXPECT_EQ(store.message(30)->encrypted, QByteArray("\x01sealed", 7));
    EXPECT_TRUE(store.updateMessage(30, QString(), 5, {}, QByteArray("\x02again", 7)));
    EXPECT_EQ(store.message(30)->encrypted, QByteArray("\x02again", 7));
    ASSERT_TRUE(store.addDeviceKey({1, QByteArray(32, 'k'), 1}));
    EXPECT_TRUE(store.addDeviceKey({1, QByteArray(32, 'k'), 2})) << "republishing is harmless";
    ASSERT_TRUE(store.addDeviceKey({1, QByteArray(32, 'j'), 3}));
    EXPECT_EQ(store.deviceKeys({1}).size(), 2u);
    EXPECT_TRUE(store.removeDeviceKey(1, QByteArray(32, 'k')));
    ASSERT_EQ(store.deviceKeys({1}).size(), 1u);
    EXPECT_EQ(store.deviceKeys({1})[0].publicKey, QByteArray(32, 'j'));
}

TEST(ServerStore, OAuthIdentitiesLinkToExactlyOneUser)
{
    QTemporaryDir dir;
    server::Store store;
    QString error;
    ASSERT_TRUE(store.open(dir.filePath(QStringLiteral("s.db")), &error));
    ASSERT_TRUE(store.insertUser({1, QStringLiteral("alice"), QStringLiteral("Alice"), {}, {}, 0}));
    ASSERT_TRUE(store.insertUser({2, QStringLiteral("bob"), QStringLiteral("Bob"), {}, {}, 0}));

    EXPECT_FALSE(store.oauthIdentity(QStringLiteral("discord"), QStringLiteral("999")).has_value());
    ASSERT_TRUE(store.insertOAuthIdentity({1, 1, QStringLiteral("discord"), QStringLiteral("999"),
        QStringLiteral("alice#0001"), 1000}));
    const auto identity = store.oauthIdentity(QStringLiteral("discord"), QStringLiteral("999"));
    ASSERT_TRUE(identity.has_value());
    EXPECT_EQ(identity->userId, 1u);

    // The same provider identity can never link to a second local user.
    EXPECT_FALSE(store.insertOAuthIdentity(
        {2, 2, QStringLiteral("discord"), QStringLiteral("999"), QStringLiteral("bob"), 2000}));
    // A different provider (or a different provider user id) is unrelated.
    EXPECT_FALSE(store.oauthIdentity(QStringLiteral("github"), QStringLiteral("999")).has_value());
}

TEST(ServerStore, OAuthIdentitiesForUserAndPasswordCheck)
{
    QTemporaryDir dir;
    server::Store store;
    QString error;
    ASSERT_TRUE(store.open(dir.filePath(QStringLiteral("s.db")), &error));
    ASSERT_TRUE(store.insertUser({1, QStringLiteral("alice"), QStringLiteral("Alice"), {}, {}, 0}));
    ASSERT_TRUE(store.insertUser(
        {2, QStringLiteral("bob"), QStringLiteral("Bob"), {}, QStringLiteral("$argon2id$x"), 0}));

    EXPECT_FALSE(store.hasPassword(1)) << "OAuth-created accounts start with no password";
    EXPECT_TRUE(store.hasPassword(2));

    EXPECT_TRUE(store.oauthIdentitiesForUser(1).empty());
    ASSERT_TRUE(store.insertOAuthIdentity(
        {1, 1, QStringLiteral("discord"), QStringLiteral("d1"), QStringLiteral("alice#1"), 1000}));
    ASSERT_TRUE(store.insertOAuthIdentity(
        {2, 1, QStringLiteral("github"), QStringLiteral("g1"), QStringLiteral("alice-gh"), 2000}));
    EXPECT_EQ(store.oauthIdentitiesForUser(1).size(), 2u);

    ASSERT_TRUE(store.deleteOAuthIdentity(1, QStringLiteral("github")));
    EXPECT_EQ(store.oauthIdentitiesForUser(1).size(), 1u);
    ASSERT_TRUE(store.deleteOAuthIdentity(1, QStringLiteral("discord")));
    EXPECT_TRUE(store.oauthIdentitiesForUser(1).empty()) << "the server layer, not Store, blocks removing the last one";
}

TEST(ServerStore, MessagesPagingEditDeleteSearch)
{
    QTemporaryDir dir;
    server::Store store;
    QString error;
    ASSERT_TRUE(store.open(dir.filePath(QStringLiteral("s.db")), &error));
    ASSERT_TRUE(store.insertUser({1, QStringLiteral("a"), QStringLiteral("A"), {}, QStringLiteral("h"), 0}));
    server::ServerRecord srv;
    srv.id = 10;
    srv.name = QStringLiteral("S");
    srv.ownerId = 1;
    ASSERT_TRUE(store.insertServer(srv, 0));
    ASSERT_TRUE(store.insertChannel({20, 10, QStringLiteral("general"), server::ChannelKind::Text, 0, 0, {}, {}}, 0));
    for (server::Id id = 1000; id < 1120; ++id)
        ASSERT_TRUE(store.insertMessage({id, 20, 1, QStringLiteral("message %1").arg(id), 0, 0, false, {}, {}, {}}));

    bool more = false;
    auto page = store.messagePage(20, 0, 50, &more);
    ASSERT_EQ(page.size(), 50u);
    EXPECT_TRUE(more);
    EXPECT_EQ(page.front().id, 1119u) << "newest first";
    auto older = store.messagePage(20, page.back().id, 100, &more);
    EXPECT_EQ(older.size(), 70u);
    EXPECT_FALSE(more);

    EXPECT_TRUE(store.updateMessage(1050, QStringLiteral("unicorn sighting"), 99, {1}));
    auto hits = store.searchMessages({20}, QStringLiteral("unicorn"), 10);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].id, 1050u);
    EXPECT_EQ(hits[0].mentions.size(), 1u);
    // FTS syntax in user input is treated literally, never as operators.
    EXPECT_NO_THROW(store.searchMessages({20}, QStringLiteral("\"unbalanced OR NEAR("), 10));

    // Server-wide search spans exactly the channels it is given.
    ASSERT_TRUE(store.insertChannel({21, 10, QStringLiteral("random"), server::ChannelKind::Text, 0, 1, {}, {}}, 0));
    ASSERT_TRUE(store.insertMessage({2000, 21, 1, QStringLiteral("another unicorn"), 0, 0, false, {}, {}, {}}));
    auto both = store.searchMessages({20, 21}, QStringLiteral("unicorn"), 10);
    ASSERT_EQ(both.size(), 2u);
    EXPECT_EQ(both[0].id, 2000u) << "newest first across channels";
    EXPECT_EQ(store.searchMessages({21}, QStringLiteral("unicorn"), 10).size(), 1u);
    EXPECT_TRUE(store.searchMessages({}, QStringLiteral("unicorn"), 10).empty());

    EXPECT_TRUE(store.setReaction(1050, 1, QStringLiteral("👍"), true));
    EXPECT_TRUE(store.setReaction(1050, 1, QStringLiteral("👍"), true)); // idempotent
    auto reactions = store.reactions(1050, 1);
    ASSERT_EQ(reactions.size(), 1u);
    EXPECT_EQ(reactions[0].count, 1u);
    EXPECT_TRUE(reactions[0].me);

    EXPECT_TRUE(store.deleteMessage(1050));
    EXPECT_TRUE(store.searchMessages({20}, QStringLiteral("unicorn"), 10).empty()) << "FTS index follows deletes";
    EXPECT_TRUE(store.reactions(1050, 1).empty());
}

TEST(ServerStore, InviteConsumptionRespectsLimit)
{
    QTemporaryDir dir;
    server::Store store;
    QString error;
    ASSERT_TRUE(store.open(dir.filePath(QStringLiteral("s.db")), &error));
    ASSERT_TRUE(store.insertUser({1, QStringLiteral("a"), QStringLiteral("A"), {}, QStringLiteral("h"), 0}));
    server::ServerRecord srv;
    srv.id = 10;
    srv.name = QStringLiteral("S");
    srv.ownerId = 1;
    ASSERT_TRUE(store.insertServer(srv, 0));
    ASSERT_TRUE(store.insertInvite({QStringLiteral("tok"), 10, 1, 0, 0, 2, 0}));
    EXPECT_TRUE(store.consumeInvite(QStringLiteral("tok")));
    EXPECT_TRUE(store.consumeInvite(QStringLiteral("tok")));
    EXPECT_FALSE(store.consumeInvite(QStringLiteral("tok")));
    EXPECT_EQ(store.inviteByToken(QStringLiteral("tok"))->uses, 2u);
}

TEST(ServerStore, SnapshotRestoresGuildStructure)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("s.db"));
    {
        server::Store store;
        QString error;
        ASSERT_TRUE(store.open(path, &error));
        ASSERT_TRUE(store.insertUser({1, QStringLiteral("a"), QStringLiteral("A"), {}, QStringLiteral("h"), 0}));
        server::ServerRecord srv;
        srv.id = 10;
        srv.name = QStringLiteral("S");
        srv.ownerId = 1;
        ASSERT_TRUE(store.insertServer(srv, 0));
        ASSERT_TRUE(store.insertRole({11, 10, QStringLiteral("Guest"), 1, 0, 0, true}));
        ASSERT_TRUE(store.insertMember({10, 1, 0, {11}}));
        ASSERT_TRUE(
            store.insertChannel({20, 10, QStringLiteral("general"), server::ChannelKind::Text, 0, 0, {}, {}}, 0));
        ASSERT_TRUE(store.upsertOverride({20, 0, 11, 1, 2}));
        ASSERT_TRUE(store.insertBan(10, 99, 1, QStringLiteral("spam"), 0));
    }
    server::Store store;
    QString error;
    ASSERT_TRUE(store.open(path, &error));
    const auto snap = store.loadSnapshot();
    ASSERT_EQ(snap.servers.size(), 1u);
    EXPECT_EQ(snap.servers[0].defaultRoleId, 11u);
    EXPECT_TRUE(snap.servers[0].members.at(1).roles.contains(11));
    EXPECT_TRUE(snap.servers[0].bans.contains(99));
    EXPECT_TRUE(snap.servers[0].channels.contains(20));
    ASSERT_EQ(snap.overrides.size(), 1u);
    EXPECT_EQ(snap.overrides[0].deny, 2u);
}

TEST(LocalStore, AccountsVolumesMutes)
{
    QTemporaryDir dir;
    daemon::LocalStore store;
    QString error;
    ASSERT_TRUE(store.open(dir.filePath(QStringLiteral("l.db")), &error));
    const auto id = store.addAccount({0, QStringLiteral("chat.example"), 6473, QStringLiteral("alice"), {}, 0});
    ASSERT_GT(id, 0);
    EXPECT_EQ(store.addAccount({0, QStringLiteral("chat.example"), 6473, QStringLiteral("alice"), {}, 0}), 0)
        << "duplicate account rejected";
    EXPECT_TRUE(store.setTrustedFingerprint(id, QStringLiteral("SHA256:AA")));
    EXPECT_EQ(store.account(id)->trustedFingerprint, QStringLiteral("SHA256:AA"));
    EXPECT_TRUE(store.setUserVolume(id, 42, 1.5));
    EXPECT_TRUE(store.setUserVolume(id, 42, 0.5));
    EXPECT_DOUBLE_EQ(store.userVolumes(id).at(42), 0.5);
    EXPECT_TRUE(store.setChannelMuted(id, 7, true));
    EXPECT_TRUE(store.mutedChannels(id).contains(7));
    EXPECT_TRUE(store.removeAccount(id));
    EXPECT_TRUE(store.userVolumes(id).empty()) << "cascade delete";
}
