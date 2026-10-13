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
    EXPECT_EQ(store.schemaVersion(), 15);
    const QString instance = store.instanceId();
    EXPECT_FALSE(instance.isEmpty());
    EXPECT_EQ(store.instanceId(), instance);
    server::UserRecord u{
        1, QStringLiteral("alice"), QStringLiteral("Alice"), QString(), QStringLiteral("$argon2id$x"), 5};
    EXPECT_TRUE(store.insertUser(u)) << "null avatar must be stored as empty string";
    EXPECT_FALSE(store.insertUser(u)) << "duplicate username rejected";
    EXPECT_EQ(store.userByName(QStringLiteral("alice"))->displayName, QStringLiteral("Alice"));
    EXPECT_TRUE(store.updateUserProfile(
        1, QStringLiteral("Alice Two"), QStringLiteral("https://example.org/a.png"), QStringLiteral("A short bio")));
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
        ASSERT_TRUE(q.exec(QStringLiteral("DROP TRIGGER message_operation_tombstone")));
        ASSERT_TRUE(q.exec(QStringLiteral("DROP TABLE message_operations")));
        ASSERT_TRUE(q.exec(QStringLiteral("DROP TABLE read_markers")));
        ASSERT_TRUE(q.exec(QStringLiteral("DROP TABLE emoji")));
        ASSERT_TRUE(q.exec(QStringLiteral("DROP TABLE attachments")));
        ASSERT_TRUE(q.exec(QStringLiteral("DROP TABLE device_keys")));
        ASSERT_TRUE(q.exec(QStringLiteral("DROP TABLE oauth_identities")));
        ASSERT_TRUE(q.exec(QStringLiteral("DROP TABLE discord_import_map")));
        ASSERT_TRUE(q.exec(QStringLiteral("DROP TABLE discord_import_replies")));
        ASSERT_TRUE(q.exec(QStringLiteral("DROP TABLE instance_audit")));
        ASSERT_TRUE(q.exec(QStringLiteral("DROP TABLE instance_suspensions")));
        ASSERT_TRUE(q.exec(QStringLiteral("DROP TABLE instance_settings")));
        ASSERT_TRUE(q.exec(QStringLiteral("ALTER TABLE users DROP COLUMN bio")));
        ASSERT_TRUE(q.exec(QStringLiteral("ALTER TABLE servers DROP COLUMN description")));
        ASSERT_TRUE(q.exec(QStringLiteral("ALTER TABLE servers DROP COLUMN icon_attachment_id")));
        ASSERT_TRUE(q.exec(QStringLiteral("ALTER TABLE servers DROP COLUMN banner_attachment_id")));
        ASSERT_TRUE(q.exec(QStringLiteral("ALTER TABLE channels DROP COLUMN description")));
        ASSERT_TRUE(q.exec(QStringLiteral("ALTER TABLE channels DROP COLUMN icon_attachment_id")));
        ASSERT_TRUE(q.exec(QStringLiteral("ALTER TABLE channels DROP COLUMN banner_attachment_id")));
        ASSERT_TRUE(q.exec(QStringLiteral("DROP INDEX messages_by_channel_time")));
        ASSERT_TRUE(q.exec(QStringLiteral("ALTER TABLE messages DROP COLUMN created_at")));
        ASSERT_TRUE(q.exec(QStringLiteral("ALTER TABLE messages DROP COLUMN encrypted")));
        ASSERT_TRUE(q.exec(QStringLiteral("PRAGMA user_version=1")));
        q.finish();
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("rewind"));

    server::Store store;
    QString error;
    ASSERT_TRUE(store.open(path, &error)) << error.toStdString();
    EXPECT_EQ(store.schemaVersion(), 15);
    EXPECT_TRUE(store.userByName(QStringLiteral("a")).has_value()) << "existing data survives";
    EXPECT_EQ(store.pendingAttachmentCount(1), 0);

    // v3: encrypted payloads and device keys.
    server::ServerRecord srv;
    srv.id = 10;
    srv.name = QStringLiteral("S");
    srv.ownerId = 1;
    ASSERT_TRUE(store.insertServer(srv, 0));
    // v13: server identity (description plus icon/banner artwork).
    auto servers = store.loadSnapshot().servers;
    ASSERT_EQ(servers.size(), 1u);
    EXPECT_TRUE(servers.front().description.isEmpty());
    auto updatedServer = servers.front();
    updatedServer.description = QStringLiteral("A cozy place for the team");
    updatedServer.iconAttachmentId = 81;
    updatedServer.bannerAttachmentId = 82;
    ASSERT_TRUE(store.updateServer(updatedServer));
    const auto reloadedServers = store.loadSnapshot().servers;
    ASSERT_EQ(reloadedServers.size(), 1u);
    EXPECT_EQ(reloadedServers.front().description, updatedServer.description);
    EXPECT_EQ(reloadedServers.front().iconAttachmentId, 81u);
    EXPECT_EQ(reloadedServers.front().bannerAttachmentId, 82u);
    EXPECT_EQ(store.artworkServer(81), 10u);
    EXPECT_EQ(store.artworkServer(82), 10u);
    ASSERT_TRUE(store.insertChannel({20, 10, QStringLiteral("general"), server::ChannelKind::Text, 0, 0, {}, {}}, 0));
    const auto channels = store.loadSnapshot().channels;
    ASSERT_EQ(channels.size(), 1u);
    EXPECT_TRUE(channels.front().description.isEmpty());
    auto updatedChannel = channels.front();
    updatedChannel.description = QStringLiteral("A longer channel guide");
    updatedChannel.iconAttachmentId = 91;
    updatedChannel.bannerAttachmentId = 92;
    ASSERT_TRUE(store.updateChannel(updatedChannel));
    EXPECT_EQ(store.loadSnapshot().channels.front().description, updatedChannel.description);
    EXPECT_EQ(store.loadSnapshot().channels.front().iconAttachmentId, 91u);
    EXPECT_EQ(store.loadSnapshot().channels.front().bannerAttachmentId, 92u);
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
    ASSERT_TRUE(store.insertOAuthIdentity(
        {1, 1, QStringLiteral("discord"), QStringLiteral("999"), QStringLiteral("alice#0001"), 1000}));
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
    ASSERT_TRUE(
        store.insertUser({2, QStringLiteral("bob"), QStringLiteral("Bob"), {}, QStringLiteral("$argon2id$x"), 0}));

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

TEST(ServerStore, ImportedHistoryUsesOriginalTimeForPagingAndSearch)
{
    QTemporaryDir dir;
    server::Store store;
    QString error;
    ASSERT_TRUE(store.open(dir.filePath(QStringLiteral("s.db")), &error));
    ASSERT_TRUE(store.insertUser({1, QStringLiteral("a"), QStringLiteral("A"), {}, {}, 0}));
    server::ServerRecord srv;
    srv.id = 10;
    srv.name = QStringLiteral("S");
    srv.ownerId = 1;
    ASSERT_TRUE(store.insertServer(srv, 0));
    ASSERT_TRUE(store.insertChannel({20, 10, QStringLiteral("general"), server::ChannelKind::Text, 0, 0, {}, {}}, 0));
    server::MessageRecord recent{100, 20, 1, QStringLiteral("unicorn today"), 0, 0, false, {}, {}, {}};
    recent.createdAt = 1800000000000;
    server::MessageRecord old{200, 20, 1, QStringLiteral("unicorn past"), 0, 0, false, {}, {}, {}};
    old.createdAt = 1577836800000; // 2020, before the snowflake epoch
    ASSERT_TRUE(store.insertMessage(recent));
    ASSERT_TRUE(store.insertMessage(old));
    bool more = false;
    auto first = store.messagePage(20, 0, 1, &more);
    ASSERT_EQ(first.size(), 1u);
    EXPECT_EQ(first[0].id, 100u);
    EXPECT_TRUE(more);
    auto second = store.messagePage(20, first[0].id, 1, &more);
    ASSERT_EQ(second.size(), 1u);
    EXPECT_EQ(second[0].createdAt, 1577836800000);
    EXPECT_FALSE(more);
    auto hits = store.searchMessages({20}, QStringLiteral("unicorn"), 2);
    ASSERT_EQ(hits.size(), 2u);
    EXPECT_EQ(hits[0].id, 100u);
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
    ASSERT_TRUE(store.findAccount(QStringLiteral("CHAT.EXAMPLE"), 6473, QStringLiteral("alice")));
    EXPECT_EQ(store.findAccount(QStringLiteral("CHAT.EXAMPLE"), 6473, QStringLiteral("alice"))->id, id);
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

TEST(ServerStore, LoginSessionPagesAndScopedRevocationPersist)
{
    QTemporaryDir dir;
    const auto path = dir.filePath(QStringLiteral("sessions.db"));
    {
        server::Store store;
        QString error;
        ASSERT_TRUE(store.open(path, &error));
        ASSERT_TRUE(
            store.insertUser({1, QStringLiteral("alice"), QStringLiteral("Alice"), {}, QStringLiteral("h"), 0}));
        ASSERT_TRUE(store.insertUser({2, QStringLiteral("bob"), QStringLiteral("Bob"), {}, QStringLiteral("h"), 0}));
        for (std::uint64_t id = 1; id <= 205; ++id)
            ASSERT_TRUE(store.insertSession({id, 1, QByteArray::number(id), 5000}));
        ASSERT_TRUE(store.insertSession({206, 1, QByteArray("expired"), 99}));
        ASSERT_TRUE(store.insertSession({207, 2, QByteArray("foreign"), 5000}));
        auto first = store.loginSessions(1, 0, 100);
        ASSERT_TRUE(first);
        ASSERT_EQ(first->size(), 101u); // 100 visible + a has-more sentinel
        EXPECT_EQ(first->front().id, 205u);
        EXPECT_EQ((*first)[99].id, 106u);
        EXPECT_GT(first->front().createdAt, 0);
        EXPECT_TRUE(first->front().refreshDigest.isEmpty());
        auto second = store.loginSessions(1, 106, 100);
        ASSERT_TRUE(second);
        EXPECT_EQ(second->size(), 101u);
        EXPECT_EQ(second->front().id, 105u);
        auto last = store.loginSessions(1, 6, 100);
        ASSERT_TRUE(last);
        EXPECT_EQ(last->size(), 5u);
        EXPECT_TRUE(store.sessionActive(205, 1, 100));
        EXPECT_FALSE(store.sessionActive(205, 2, 100));
        EXPECT_FALSE(store.sessionActive(206, 1, 100));
        EXPECT_EQ(store.revokeLoginSession(1, 207), std::optional<bool>(false));
        EXPECT_EQ(store.revokeLoginSession(1, 205), std::optional<bool>(true));
        EXPECT_EQ(store.revokeLoginSession(1, 205), std::optional<bool>(false));
    }
    server::Store reopened;
    QString error;
    ASSERT_TRUE(reopened.open(path, &error));
    EXPECT_FALSE(reopened.sessionActive(205, 1, 100));
    EXPECT_FALSE(reopened.sessionByDigest(QByteArray("205")));
    EXPECT_TRUE(reopened.sessionActive(207, 2, 100));
}

TEST(ServerStore, VersionFourteenArtworkMigratesWithoutChangingReferences)
{
    QTemporaryDir dir;
    const QString path = dir.filePath("artwork.db");
    {
        server::Store store;
        QString error;
        ASSERT_TRUE(store.open(path, &error));
        ASSERT_TRUE(store.insertUser({1, "owner", "Owner", {}, {}, 0}));
        server::ServerRecord community;
        community.id = 10;
        community.name = "Artwork";
        community.ownerId = 1;
        ASSERT_TRUE(store.insertServer(community, 0));
        server::ChannelRecord channel;
        channel.id = 20;
        channel.serverId = 10;
        channel.name = "Images";
        ASSERT_TRUE(store.insertChannel(channel, 0));
        server::AttachmentRecord attachment;
        attachment.id = 30;
        attachment.channelId = 20;
        attachment.uploaderId = 1;
        attachment.filename = "icon.png";
        attachment.mimeType = "image/png";
        attachment.size = 20;
        attachment.sha256 = QByteArray(32, 'a');
        attachment.artwork = true;
        ASSERT_TRUE(store.insertAttachment(attachment));
        channel.iconAttachmentId = 30;
        ASSERT_TRUE(store.updateChannel(channel));
        ASSERT_TRUE(store.insertEmoji({40, 10, "kept", 30, 1, 0}));
    }
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", "artwork-v14-fixture");
        db.setDatabaseName(path);
        ASSERT_TRUE(db.open());
        QSqlQuery query(db);
        // Recreate the actual v14 CASCADE schema, with a dependent emoji row.
        ASSERT_TRUE(query.exec(R"(CREATE TABLE attachments_old(
            id INTEGER PRIMARY KEY,
            channel_id INTEGER NOT NULL REFERENCES channels(id) ON DELETE CASCADE,
            uploader_id INTEGER NOT NULL,
            message_id INTEGER REFERENCES messages(id) ON DELETE CASCADE,
            filename TEXT NOT NULL, mime_type TEXT NOT NULL, size INTEGER NOT NULL,
            sha256 BLOB NOT NULL, created_at INTEGER NOT NULL, artwork INTEGER NOT NULL DEFAULT 0))"));
        ASSERT_TRUE(query.exec("INSERT INTO attachments_old SELECT id, channel_id, uploader_id, message_id, "
                               "filename, mime_type, size, sha256, created_at, artwork FROM attachments"));
        ASSERT_TRUE(query.exec("DROP TABLE attachments"));
        ASSERT_TRUE(query.exec("ALTER TABLE attachments_old RENAME TO attachments"));
        ASSERT_TRUE(query.exec("CREATE INDEX attachments_by_message ON attachments(message_id)"));
        ASSERT_TRUE(
            query.exec("CREATE INDEX attachments_pending ON attachments(uploader_id) WHERE message_id IS NULL"));
        ASSERT_TRUE(query.exec("PRAGMA user_version=14"));
        db.close();
    }
    QSqlDatabase::removeDatabase("artwork-v14-fixture");
    server::Store migrated;
    QString error;
    ASSERT_TRUE(migrated.open(path, &error)) << error.toStdString();
    EXPECT_EQ(migrated.schemaVersion(), 15);
    ASSERT_TRUE(migrated.attachment(30));
    EXPECT_TRUE(migrated.attachment(30)->artworkFraming.isEmpty());
    EXPECT_EQ(migrated.artworkChannel(30), 20u);
    ASSERT_TRUE(migrated.emoji(40));
    EXPECT_EQ(migrated.emoji(40)->attachmentId, 30u);
    EXPECT_EQ(migrated.emojiFor(10).size(), 1u);
    auto second = *migrated.attachment(30);
    second.id = 31;
    second.artworkFraming = QByteArray("immutable-framing");
    ASSERT_TRUE(migrated.insertAttachment(second));
    EXPECT_EQ(migrated.attachment(31)->artworkFraming, second.artworkFraming);
}

TEST(ServerStore, ServerArtworkOutlivesAllChannelsAndOrphansAreReclaimed)
{
    QTemporaryDir dir;
    server::Store store;
    QString error;
    ASSERT_TRUE(store.open(dir.filePath("lifetime.db"), &error)) << error.toStdString();
    ASSERT_TRUE(store.insertUser({1, "owner", "Owner", {}, {}, 0}));
    server::ServerRecord community;
    community.id = 10;
    community.name = "Artwork";
    community.ownerId = 1;
    ASSERT_TRUE(store.insertServer(community, 0));
    server::ChannelRecord carrier;
    carrier.id = 20;
    carrier.serverId = 10;
    carrier.name = "Carrier";
    ASSERT_TRUE(store.insertChannel(carrier, 0));
    auto last = carrier;
    last.id = 21;
    ASSERT_TRUE(store.insertChannel(last, 0));
    server::AttachmentRecord asset;
    asset.channelId = 20;
    asset.uploaderId = 1;
    asset.filename = "image.png";
    asset.mimeType = "image/png";
    asset.sha256 = QByteArray(32, 'a');
    asset.artwork = true;
    asset.artworkFraming = "framing";
    for (server::Id id : {30u, 31u, 32u, 33u}) {
        asset.id = id;
        ASSERT_TRUE(store.insertAttachment(asset));
    }
    community.iconAttachmentId = 30;
    community.bannerAttachmentId = 31;
    ASSERT_TRUE(store.updateServer(community));
    carrier.iconAttachmentId = 32;
    ASSERT_TRUE(store.updateChannel(carrier));
    ASSERT_TRUE(store.insertEmoji({40, 10, "kept", 33, 1, 0}));
    // Pending ordinary uploads still disappear with their upload channel.
    asset.id = 34;
    asset.artwork = false;
    ASSERT_TRUE(store.insertAttachment(asset));
    EXPECT_EQ(store.pendingAttachmentCount(1), 1);
    // Attached message files still follow the message's CASCADE lifetime.
    asset.id = 35;
    ASSERT_TRUE(store.insertAttachment(asset));
    server::MessageRecord message{60, 20, 1, "message", 0, 0, false, {}, {*store.attachment(35)}, {}};
    ASSERT_TRUE(store.insertMessage(message));
    ASSERT_TRUE(store.deleteMessage(60));
    EXPECT_FALSE(store.attachment(35));
    ASSERT_TRUE(store.deleteChannel(20));
    EXPECT_FALSE(store.attachment(34));
    EXPECT_FALSE(store.attachment(32));
    for (server::Id id : {30u, 31u, 33u}) {
        ASSERT_TRUE(store.attachment(id));
        EXPECT_EQ(store.attachment(id)->channelId, 0u);
        EXPECT_EQ(store.attachment(id)->artworkFraming, QByteArray("framing"));
    }
    ASSERT_TRUE(store.deleteChannel(21));
    EXPECT_TRUE(store.attachment(30));
    EXPECT_TRUE(store.attachment(31));
    EXPECT_EQ(store.artworkServer(30), 10u);
    EXPECT_TRUE(store.purgePendingAttachments(100).empty());
    ASSERT_TRUE(store.deleteServer(10));
    EXPECT_TRUE(store.allAttachmentIds().empty());
    EXPECT_FALSE(store.emoji(40));
    // Enforcement was restored after the rebuild migration.
    asset.id = 50;
    asset.channelId = 999;
    EXPECT_FALSE(store.insertAttachment(asset));
}

TEST(ServerStore, FailedArtworkMigrationRollsBackAndRestoresForeignKeys)
{
    QTemporaryDir dir;
    const auto path = dir.filePath("broken-artwork.db");
    {
        server::Store seed;
        QString error;
        ASSERT_TRUE(seed.open(path, &error));
        ASSERT_TRUE(seed.insertUser({1, "owner", "Owner", {}, {}, 0}));
        server::ServerRecord community;
        community.id = 10;
        community.name = "Artwork";
        community.ownerId = 1;
        ASSERT_TRUE(seed.insertServer(community, 0));
    }
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", "broken-artwork-fixture");
        db.setDatabaseName(path);
        ASSERT_TRUE(db.open());
        QSqlQuery query(db);
        ASSERT_TRUE(query.exec("ALTER TABLE attachments DROP COLUMN artwork_framing"));
        ASSERT_TRUE(query.exec("INSERT INTO emoji VALUES(40, 10, 'broken', 999, 1, 0)"));
        ASSERT_TRUE(query.exec("PRAGMA user_version=14"));
        db.close();
    }
    QSqlDatabase::removeDatabase("broken-artwork-fixture");
    const auto existingConnections = QSqlDatabase::connectionNames();
    server::Store migrated;
    QString error;
    EXPECT_FALSE(migrated.open(path, &error));
    EXPECT_TRUE(error.contains("foreign key check"));
    EXPECT_EQ(migrated.schemaVersion(), 14);
    for (const auto& name : QSqlDatabase::connectionNames()) {
        if (existingConnections.contains(name))
            continue;
        const auto db = QSqlDatabase::database(name);
        QSqlQuery query(db);
        ASSERT_TRUE(query.exec("PRAGMA foreign_keys"));
        ASSERT_TRUE(query.next());
        EXPECT_EQ(query.value(0).toInt(), 1);
        ASSERT_TRUE(query.exec("SELECT COUNT(*) FROM emoji WHERE id=40"));
        ASSERT_TRUE(query.next());
        EXPECT_EQ(query.value(0).toInt(), 1);
        ASSERT_TRUE(query.exec("SELECT COUNT(*) FROM sqlite_master WHERE name='attachments_v15'"));
        ASSERT_TRUE(query.next());
        EXPECT_EQ(query.value(0).toInt(), 0);
    }
}
