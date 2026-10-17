#include "Harness.hpp"
#include "omachat/core/Permissions.hpp"

#include <algorithm>
#include <gtest/gtest.h>

using namespace omachat;
using namespace omachat::test;

namespace {
class ChannelPlacement : public ::testing::Test {
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
        const auto auth = owner->registerUser("placement-owner", "placement-password");
        ASSERT_TRUE(auth);
        const auto memberAuth = member->registerUser("placement-member", "placement-password");
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
            if (user.username() == "placement-member")
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
    proto::Envelope place(RawClient& client, std::uint64_t channel, std::uint64_t parent, std::uint64_t before)
    {
        proto::Envelope request;
        auto* move = request.mutable_update_channel();
        move->set_channel_id(channel);
        move->set_set_parent(true);
        move->set_parent_id(parent);
        move->set_set_before(true);
        move->set_before_id(before);
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

TEST_F(ChannelPlacement, ResolvesStableSiblingAcrossHiddenChannelsAndInterleavedCategoryPositions)
{
    const auto source = create("source", proto::CHANNEL_TYPE_TEXT);
    const auto category = create("Interleaved", proto::CHANNEL_TYPE_CATEGORY);
    const auto hidden = create("hidden", proto::CHANNEL_TYPE_TEXT);
    const auto target = create("target", proto::CHANNEL_TYPE_TEXT);
    overrideFor(source.id(), permissions::ManageChannel, 0);
    overrideFor(hidden.id(), 0, permissions::ViewChannel);
    EXPECT_EQ(find(sync(*member), hidden.id()).id(), 0u);
    const auto moved = place(*member, source.id(), 0, target.id());
    ASSERT_TRUE(moved.has_channel()) << moved.error().message();
    const auto state = sync(*owner);
    EXPECT_EQ(find(state, source.id()).position() + 1, find(state, target.id()).position());
    EXPECT_LT(find(state, hidden.id()).position(), find(state, source.id()).position());
    EXPECT_EQ(find(state, category.id()).type(), proto::CHANNEL_TYPE_CATEGORY);
    const auto appended = place(*member, source.id(), 0, 0);
    ASSERT_TRUE(appended.has_channel()) << appended.error().message();
    EXPECT_GT(appended.channel().position(), find(sync(*owner), target.id()).position());
}

TEST_F(ChannelPlacement, RejectsMissingWrongDestinationSelfAndHiddenTargetsWithoutMovingSource)
{
    const auto category = create("Destination", proto::CHANNEL_TYPE_CATEGORY);
    const auto source = create("source", proto::CHANNEL_TYPE_TEXT);
    const auto target = create("target", proto::CHANNEL_TYPE_TEXT, category.id());
    overrideFor(source.id(), permissions::ManageChannel, 0);
    overrideFor(target.id(), 0, permissions::ViewChannel);
    for (const auto before : {std::uint64_t(999999999), source.id(), target.id()}) {
        const auto rejected = place(*member, source.id(), 0, before);
        ASSERT_TRUE(rejected.has_error());
        EXPECT_EQ(rejected.error().code(), proto::ERROR_BAD_REQUEST);
        const auto unchanged = find(sync(*owner), source.id());
        EXPECT_EQ(unchanged.parent_id(), source.parent_id());
        EXPECT_EQ(unchanged.position(), source.position());
    }
    // Destination and sibling resolution reject the inaccessible target before mutation.
    const auto hiddenRejected = place(*member, source.id(), category.id(), target.id());
    ASSERT_TRUE(hiddenRejected.has_error());
    EXPECT_EQ(find(sync(*owner), source.id()).parent_id(), 0u);
    const auto wrongType = place(*owner, category.id(), 0, source.id());
    ASSERT_TRUE(wrongType.has_error());
    const auto nested = place(*owner, category.id(), category.id(), 0);
    ASSERT_TRUE(nested.has_error());
}

TEST_F(ChannelPlacement, PreservesPermissionGateAndRejectsConflictingNumericPosition)
{
    const auto source = create("source", proto::CHANNEL_TYPE_TEXT);
    const auto other = create("other", proto::CHANNEL_TYPE_TEXT);
    const auto denied = place(*member, source.id(), 0, other.id());
    ASSERT_TRUE(denied.has_error());
    EXPECT_EQ(denied.error().code(), proto::ERROR_PERMISSION_DENIED);
    proto::Envelope request;
    auto* move = request.mutable_update_channel();
    move->set_channel_id(source.id());
    move->set_set_before(true);
    move->set_before_id(other.id());
    move->set_set_position(true);
    move->set_position(0);
    const auto conflict = owner->call(request);
    ASSERT_TRUE(conflict && conflict->has_error());
    EXPECT_EQ(conflict->error().code(), proto::ERROR_BAD_REQUEST);
    EXPECT_EQ(find(sync(*owner), source.id()).position(), source.position());
}
