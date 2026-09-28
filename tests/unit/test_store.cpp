#include "storage/LocalStore.hpp"
#include "storage/Store.hpp"

#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace omachat;

TEST(ServerStore, UsersSessionsAndConflicts)
{
    QTemporaryDir dir;
    server::Store store;
    QString error;
    ASSERT_TRUE(store.open(dir.filePath(QStringLiteral("s.db")), &error)) << error.toStdString();
    EXPECT_EQ(store.schemaVersion(), 1);
    server::UserRecord u{
        1, QStringLiteral("alice"), QStringLiteral("Alice"), QString(), QStringLiteral("$argon2id$x"), 5};
    EXPECT_TRUE(store.insertUser(u)) << "null avatar must be stored as empty string";
    EXPECT_FALSE(store.insertUser(u)) << "duplicate username rejected";
    EXPECT_EQ(store.userByName(QStringLiteral("alice"))->displayName, QStringLiteral("Alice"));

    server::SessionRecord s{7, 1, QByteArray(32, 'k'), 1000};
    EXPECT_TRUE(store.insertSession(s));
    EXPECT_EQ(store.sessionByDigest(QByteArray(32, 'k'))->userId, 1u);
    EXPECT_TRUE(store.rotateSession(7, QByteArray(32, 'n'), 2000));
    EXPECT_FALSE(store.sessionByDigest(QByteArray(32, 'k')).has_value()) << "old refresh token is dead";
    EXPECT_EQ(store.purgeExpiredSessions(5000), 1);
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
        ASSERT_TRUE(store.insertMessage({id, 20, 1, QStringLiteral("message %1").arg(id), 0, 0, false, {}}));

    bool more = false;
    auto page = store.messagePage(20, 0, 50, &more);
    ASSERT_EQ(page.size(), 50u);
    EXPECT_TRUE(more);
    EXPECT_EQ(page.front().id, 1119u) << "newest first";
    auto older = store.messagePage(20, page.back().id, 100, &more);
    EXPECT_EQ(older.size(), 70u);
    EXPECT_FALSE(more);

    EXPECT_TRUE(store.updateMessage(1050, QStringLiteral("unicorn sighting"), 99, {1}));
    auto hits = store.searchMessages(20, QStringLiteral("unicorn"), 10);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].id, 1050u);
    EXPECT_EQ(hits[0].mentions.size(), 1u);
    // FTS syntax in user input is treated literally, never as operators.
    EXPECT_NO_THROW(store.searchMessages(20, QStringLiteral("\"unbalanced OR NEAR("), 10));

    EXPECT_TRUE(store.setReaction(1050, 1, QStringLiteral("👍"), true));
    EXPECT_TRUE(store.setReaction(1050, 1, QStringLiteral("👍"), true)); // idempotent
    auto reactions = store.reactions(1050, 1);
    ASSERT_EQ(reactions.size(), 1u);
    EXPECT_EQ(reactions[0].count, 1u);
    EXPECT_TRUE(reactions[0].me);

    EXPECT_TRUE(store.deleteMessage(1050));
    EXPECT_TRUE(store.searchMessages(20, QStringLiteral("unicorn"), 10).empty()) << "FTS index follows deletes";
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
