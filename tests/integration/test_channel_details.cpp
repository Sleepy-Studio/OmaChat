#include "Harness.hpp"
#include "omachat/core/Permissions.hpp"

#include <algorithm>
#include <gtest/gtest.h>

using namespace omachat;
using namespace omachat::test;

namespace {
class ChannelDetails : public ::testing::Test {
protected:
    TestServer server;
    std::unique_ptr<RawClient> owner, member;
    std::uint64_t serverId = 0, memberId = 0;

    void SetUp() override
    {
        ASSERT_TRUE(server.start());
        owner = std::make_unique<RawClient>(server);
        member = std::make_unique<RawClient>(server);
        ASSERT_TRUE(owner->connect());
        ASSERT_TRUE(owner->hello());
        ASSERT_TRUE(member->connect());
        ASSERT_TRUE(member->hello());
        const auto auth = owner->registerUser("details-owner", "placement-password");
        ASSERT_TRUE(auth);
        const auto memberAuth = member->registerUser("details-member", "placement-password");
        ASSERT_TRUE(memberAuth);
        proto::Envelope request;
        request.mutable_create_server()->set_name("Channel placement");
        auto reply = owner->call(request);
        ASSERT_TRUE(reply && reply->has_server());
        serverId = reply->server().id();
        request.Clear();
        request.mutable_create_invite()->set_server_id(serverId);
        reply = owner->call(request);
        ASSERT_TRUE(reply && reply->has_invite());
        const auto token = reply->invite().token();
        request.Clear();
        request.mutable_join_invite()->set_token(token);
        reply = member->call(request);
        ASSERT_TRUE(reply && reply->has_server());
        const auto state = sync(*owner);
        for (const auto& user : state.users())
            if (user.username() == "details-member")
                memberId = user.id();
        ASSERT_NE(memberId, 0u);
    }
    proto::SyncState sync(RawClient& client)
    {
        proto::Envelope request;
        request.mutable_sync();
        const auto reply = client.call(request);
        EXPECT_TRUE(reply && reply->has_sync_state());
        return reply && reply->has_sync_state() ? reply->sync_state() : proto::SyncState{};
    }
    proto::Channel create(const std::string& name, proto::ChannelType type, std::uint64_t parent = 0)
    {
        proto::Envelope request;
        auto* c = request.mutable_create_channel();
        c->set_server_id(serverId);
        c->set_name(name);
        c->set_type(type);
        c->set_parent_id(parent);
        const auto reply = owner->call(request);
        EXPECT_TRUE(reply && reply->has_channel());
        return reply && reply->has_channel() ? reply->channel() : proto::Channel{};
    }
    proto::Envelope edit(RawClient& client, const proto::Channel& original, const std::string& name = "edited")
    {
        proto::Envelope request;
        auto* update = request.mutable_update_channel();
        update->set_channel_id(original.id());
        update->set_check_details(true);
        update->set_expected_name(original.name());
        update->set_expected_topic(original.topic());
        update->set_expected_description(original.description());
        update->set_name(name);
        update->set_set_topic(true);
        update->set_topic("edited topic");
        update->set_set_description(true);
        update->set_description("edited description");
        return client.call(request).value_or(proto::Envelope{});
    }
    void overrideFor(std::uint64_t channel, std::uint64_t allow, std::uint64_t deny)
    {
        proto::Envelope request;
        auto* change = request.mutable_set_override()->mutable_override();
        change->set_channel_id(channel);
        change->set_target_type(proto::PermissionOverride::TARGET_USER);
        change->set_target_id(memberId);
        change->set_allow(allow);
        change->set_deny(deny);
        const auto reply = owner->call(request);
        ASSERT_TRUE(reply && reply->has_ok());
    }
    static proto::Channel find(const proto::SyncState& state, std::uint64_t id)
    {
        for (const auto& c : state.channels())
            if (c.id() == id)
                return c;
        return {};
    }
};
} // namespace

TEST_F(ChannelDetails, RejectsStaleOriginalsWithoutPartialMutationAndAllowsDeliberateRetry)
{
    const auto original = create("original", proto::CHANNEL_TYPE_TEXT);
    const auto remote = edit(*owner, original, "remote");
    ASSERT_TRUE(remote.has_channel()) << remote.error().message();
    const auto rejected = edit(*owner, original, "local");
    ASSERT_TRUE(rejected.has_error());
    EXPECT_EQ(rejected.error().code(), proto::ERROR_CONFLICT);
    EXPECT_FALSE(rejected.has_channel());
    const auto current = find(sync(*owner), original.id());
    EXPECT_EQ(current.name(), "remote");
    EXPECT_EQ(current.topic(), "edited topic");
    EXPECT_EQ(current.description(), "edited description");
    const auto retry = edit(*owner, current, "local");
    ASSERT_TRUE(retry.has_channel()) << retry.error().message();
    EXPECT_EQ(retry.channel().name(), "local");
}

TEST_F(ChannelDetails, ChecksEveryOriginalAndDoesNotCoupleArtworkOrPlacementToDetails)
{
    const auto original = create("original", proto::CHANNEL_TYPE_TEXT);
    for (int field = 0; field < 3; ++field) {
        auto stale = original;
        if (field == 0)
            stale.set_name("stale");
        if (field == 1)
            stale.set_topic("stale");
        if (field == 2)
            stale.set_description("stale");
        EXPECT_EQ(edit(*owner, stale).error().code(), proto::ERROR_CONFLICT);
    }
    proto::Envelope request;
    auto* update = request.mutable_update_channel();
    update->set_channel_id(original.id());
    update->set_check_details(true);
    update->set_expected_name(original.name());
    update->set_name("changed");
    update->set_set_position(true);
    update->set_position(0);
    const auto reply = owner->call(request);
    ASSERT_TRUE(reply && reply->has_error());
    EXPECT_EQ(reply->error().code(), proto::ERROR_BAD_REQUEST);
    EXPECT_EQ(find(sync(*owner), original.id()).name(), "original");
    // A legacy updater remains compatible; placement alone does not invalidate originals.
    update->Clear();
    update->set_channel_id(original.id());
    update->set_set_position(true);
    update->set_position(0);
    ASSERT_TRUE(owner->call(request)->has_channel());
    EXPECT_TRUE(edit(*owner, original).has_channel());
}

TEST_F(ChannelDetails, PermissionGatesPrecedeConflictAndInvalidDetailsAreAtomic)
{
    const auto original = create("original", proto::CHANNEL_TYPE_TEXT);
    auto stale = original;
    stale.set_name("private guessed state");
    EXPECT_EQ(edit(*member, stale).error().code(), proto::ERROR_PERMISSION_DENIED);
    overrideFor(original.id(), 0, permissions::ViewChannel);
    EXPECT_EQ(edit(*member, stale).error().code(), proto::ERROR_NOT_FOUND);
    const auto invalid = edit(*owner, original, std::string(65, 'x'));
    EXPECT_EQ(invalid.error().code(), proto::ERROR_BAD_REQUEST);
    const auto unchanged = find(sync(*owner), original.id());
    EXPECT_EQ(unchanged.name(), original.name());
    EXPECT_EQ(unchanged.topic(), original.topic());
    EXPECT_EQ(unchanged.description(), original.description());
}

TEST(ChannelDetailsDaemon, MapsOriginalsRejectsMalformedAndReloadsAuthoritativeDetails)
{
    TestServer server;
    ASSERT_TRUE(server.start());
    TestDaemon daemon(QStringLiteral("details-daemon"));
    ASSERT_TRUE(daemon.start());
    ASSERT_TRUE(daemon.registerOn(server, QStringLiteral("details-daemon"), QStringLiteral("details-password")));
    auto community = daemon.call(QStringLiteral("server.create"), {{"name", "Details"}});
    ASSERT_TRUE(community.ok) << community.errorMessage.toStdString();
    const auto created = daemon.call(QStringLiteral("channel.create"),
        {{"server", community.result.value("id")}, {"name", "original"}, {"topic", "first"},
            {"description", "initial"}});
    ASSERT_TRUE(created.ok) << created.errorMessage.toStdString();
    const auto id = created.result.value("id");
    const QJsonObject original{{"name", "original"}, {"topic", "first"}, {"description", "initial"}};
    const auto changed = daemon.call(QStringLiteral("channel.update"),
        {{"channel", id}, {"name", "remote"}, {"topic", "second"}, {"description", "changed"}, {"original", original}});
    ASSERT_TRUE(changed.ok) << changed.errorMessage.toStdString();
    const auto conflict = daemon.call(QStringLiteral("channel.update"),
        {{"channel", id}, {"name", "local"}, {"topic", "local topic"}, {"description", "local description"},
            {"original", original}});
    EXPECT_FALSE(conflict.ok);
    EXPECT_EQ(conflict.errorCode, QStringLiteral("Conflict"));
    const auto reloaded = daemon.call(QStringLiteral("channel.details.reload"), {{"channel", id}});
    ASSERT_TRUE(reloaded.ok) << reloaded.errorMessage.toStdString();
    EXPECT_EQ(reloaded.result.value("name").toString(), QStringLiteral("remote"));
    EXPECT_EQ(reloaded.result.value("topic").toString(), QStringLiteral("second"));
    EXPECT_EQ(reloaded.result.value("description").toString(), QStringLiteral("changed"));
    for (const auto& bad :
        {QJsonObject{{"name", "remote"}}, QJsonObject{{"name", 4}, {"topic", "second"}, {"description", "changed"}}}) {
        const auto invalid
            = daemon.call(QStringLiteral("channel.update"), {{"channel", id}, {"name", "invalid"}, {"original", bad}});
        EXPECT_FALSE(invalid.ok);
        EXPECT_EQ(invalid.errorCode, QStringLiteral("BadRequest"));
    }
    const auto mixed = daemon.call(
        QStringLiteral("channel.update"), {{"channel", id}, {"original", reloaded.result}, {"position", 0}});
    EXPECT_FALSE(mixed.ok);
    EXPECT_EQ(mixed.errorCode, QStringLiteral("BadRequest"));
    const auto retry = daemon.call(
        QStringLiteral("channel.update"), {{"channel", id}, {"name", "local"}, {"original", reloaded.result}});
    ASSERT_TRUE(retry.ok) << retry.errorMessage.toStdString();
    EXPECT_EQ(retry.result.value("name").toString(), QStringLiteral("local"));
    EXPECT_EQ(retry.result.value("topic").toString(), QStringLiteral("second"));
}
