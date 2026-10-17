#include "Harness.hpp"
#include "omachat/core/Permissions.hpp"

#include <QJsonArray>
#include <algorithm>
#include <gtest/gtest.h>

using namespace omachat;
using namespace omachat::test;

namespace {
class PlacementRevision : public ::testing::Test {
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
        const auto auth = owner->registerUser("duplicate-owner", "duplicate-password");
        ASSERT_TRUE(auth);
        const auto memberAuth = member->registerUser("duplicate-member", "duplicate-password");
        ASSERT_TRUE(memberAuth);
        proto::Envelope request;
        request.mutable_create_server()->set_name("Channel duplication");
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
            if (user.username() == "duplicate-member")
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
    proto::Envelope duplicate(RawClient& client, std::uint64_t channel, const std::string& name = "copy",
        std::optional<std::uint64_t> parent = {})
    {
        proto::Envelope request;
        auto* copy = request.mutable_duplicate_channel();
        copy->set_channel_id(channel);
        copy->set_name(name);
        if (parent) {
            copy->set_set_parent(true);
            copy->set_parent_id(*parent);
        }
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

TEST_F(PlacementRevision, HiddenSiblingChangesRejectStaleListWithoutExposingHiddenMetadata)
{
    const auto source = create("visible-source", proto::CHANNEL_TYPE_TEXT);
    const auto hidden = create("hidden-sibling", proto::CHANNEL_TYPE_TEXT);
    overrideFor(source.id(), permissions::ViewChannel | permissions::ManageChannel, 0);
    overrideFor(hidden.id(), 0, permissions::ViewChannel);
    const auto observed = find(sync(*member), source.id());
    ASSERT_FALSE(observed.placement_token().empty());
    EXPECT_EQ(find(sync(*member), hidden.id()).id(), 0u);
    proto::Envelope change;
    change.mutable_update_channel()->set_channel_id(hidden.id());
    change.mutable_update_channel()->set_set_position(true);
    change.mutable_update_channel()->set_position(0);
    ASSERT_TRUE(owner->call(change)->has_channel());
    const auto before = sync(*owner);
    // Per-channel originals are deliberately fresh: the whole-list check alone must fail.
    const auto latest = find(before, source.id());
    proto::Envelope request;
    auto* move = request.mutable_update_channel();
    move->set_channel_id(source.id());
    move->set_set_before(true);
    move->set_before_id(0);
    move->set_check_placement(true);
    move->set_expected_parent_id(latest.parent_id());
    move->set_expected_position(latest.position());
    move->set_check_placement_list(true);
    move->set_expected_placement_token(observed.placement_token());
    const auto rejected = member->call(request);
    ASSERT_TRUE(rejected && rejected->has_error());
    EXPECT_EQ(rejected->error().code(), proto::ERROR_CONFLICT);
    EXPECT_EQ(rejected->error().message().find("hidden-sibling"), std::string::npos);
    for (const auto& c : before.channels()) {
        const auto unchanged = find(sync(*owner), c.id());
        EXPECT_EQ(unchanged.parent_id(), c.parent_id());
        EXPECT_EQ(unchanged.position(), c.position());
    }
    const auto reviewed = find(sync(*member), source.id());
    EXPECT_NE(reviewed.placement_token(), observed.placement_token());
    move->set_expected_placement_token(reviewed.placement_token());
    ASSERT_TRUE(member->call(request)->has_channel());
}

TEST_F(PlacementRevision, StructuralAbaInvalidatesOpaqueTokens)
{
    const auto category = create("temporary-category", proto::CHANNEL_TYPE_CATEGORY);
    const auto source = create("source", proto::CHANNEL_TYPE_TEXT);
    const auto original = find(sync(*owner), source.id());
    proto::Envelope request;
    auto* move = request.mutable_update_channel();
    move->set_channel_id(source.id());
    move->set_set_parent(true);
    move->set_parent_id(category.id());
    ASSERT_TRUE(owner->call(request)->has_channel());
    move->set_parent_id(0);
    ASSERT_TRUE(owner->call(request)->has_channel());
    const auto restored = find(sync(*owner), source.id());
    EXPECT_EQ(restored.parent_id(), original.parent_id());
    EXPECT_NE(restored.placement_token(), original.placement_token());
    move->set_check_placement_list(true);
    move->set_expected_placement_token(original.placement_token());
    const auto conflict = owner->call(request);
    ASSERT_TRUE(conflict && conflict->has_error());
    EXPECT_EQ(conflict->error().code(), proto::ERROR_CONFLICT);
}

TEST_F(PlacementRevision, CreateDuplicateDeleteRotateTokensAndDetailsKeepToken)
{
    const auto source = create("source", proto::CHANNEL_TYPE_TEXT);
    const auto original = find(sync(*owner), source.id()).placement_token();
    proto::Envelope request;
    request.mutable_update_channel()->set_channel_id(source.id());
    request.mutable_update_channel()->set_name("renamed-source");
    ASSERT_TRUE(owner->call(request)->has_channel());
    EXPECT_EQ(find(sync(*owner), source.id()).placement_token(), original);
    const auto sibling = create("sibling", proto::CHANNEL_TYPE_TEXT);
    const auto created = find(sync(*owner), source.id()).placement_token();
    EXPECT_NE(created, original);
    const auto copy = duplicate(*owner, sibling.id());
    ASSERT_TRUE(copy.has_channel());
    const auto duplicated = find(sync(*owner), source.id()).placement_token();
    EXPECT_NE(duplicated, created);
    request.Clear();
    request.mutable_delete_channel()->set_channel_id(copy.channel().id());
    ASSERT_TRUE(owner->call(request)->has_ok());
    EXPECT_NE(find(sync(*owner), source.id()).placement_token(), duplicated);
}

TEST_F(PlacementRevision, MissingTokenAndMixedScopesRejectWhileLegacyCasRemainsCompatible)
{
    const auto source = create("source", proto::CHANNEL_TYPE_TEXT);
    proto::Envelope request;
    auto* move = request.mutable_update_channel();
    move->set_channel_id(source.id());
    move->set_set_before(true);
    move->set_check_placement_list(true);
    auto reply = owner->call(request);
    ASSERT_TRUE(reply && reply->has_error());
    EXPECT_EQ(reply->error().code(), proto::ERROR_BAD_REQUEST);
    move->set_expected_placement_token(source.placement_token());
    move->set_name("mixed");
    reply = owner->call(request);
    ASSERT_TRUE(reply && reply->has_error());
    EXPECT_EQ(reply->error().code(), proto::ERROR_BAD_REQUEST);
    move->set_name("");
    move->set_check_placement_list(false);
    move->set_check_placement(true);
    move->set_expected_parent_id(source.parent_id());
    move->set_expected_position(source.position());
    ASSERT_TRUE(owner->call(request)->has_channel());
}

TEST_F(PlacementRevision, ServerRestartInvalidatesTokenWithoutChangingPersistedPlacement)
{
    const auto source = create("source", proto::CHANNEL_TYPE_TEXT);
    const auto observed = find(sync(*owner), source.id());
    owner.reset();
    member.reset();
    server.stop();
    ASSERT_TRUE(server.start());
    owner = std::make_unique<RawClient>(server);
    ASSERT_TRUE(owner->connect());
    ASSERT_TRUE(owner->hello());
    ASSERT_TRUE(owner->login("duplicate-owner", "duplicate-password"));
    const auto latest = find(sync(*owner), source.id());
    EXPECT_EQ(latest.parent_id(), observed.parent_id());
    EXPECT_EQ(latest.position(), observed.position());
    EXPECT_NE(latest.placement_token(), observed.placement_token());
    proto::Envelope request;
    auto* move = request.mutable_update_channel();
    move->set_channel_id(source.id());
    move->set_set_before(true);
    move->set_check_placement_list(true);
    move->set_expected_placement_token(observed.placement_token());
    const auto reply = owner->call(request);
    ASSERT_TRUE(reply && reply->has_error());
    EXPECT_EQ(reply->error().code(), proto::ERROR_CONFLICT);
}

TEST_F(PlacementRevision, StructuralEventsRefreshFullDaemonSnapshotWithoutPromotingPartialTokens)
{
    const auto source = create("visible-source", proto::CHANNEL_TYPE_TEXT);
    const auto hidden = create("hidden-source", proto::CHANNEL_TYPE_TEXT);
    overrideFor(source.id(), permissions::ViewChannel | permissions::ManageChannel, 0);
    overrideFor(hidden.id(), 0, permissions::ViewChannel);
    TestDaemon daemon{"placement-revision-refresh"};
    ASSERT_TRUE(daemon.start());
    ASSERT_TRUE(daemon.loginOn(server, "duplicate-member", "duplicate-password"));
    auto observed = [&] {
        QJsonObject channel;
        const auto snapshot = daemon.call("state.snapshot");
        EXPECT_TRUE(snapshot.ok);
        for (const auto& item : snapshot.result.value("channels").toArray()) {
            const auto row = item.toObject();
            EXPECT_NE(row.value("id").toString(), QString::number(hidden.id()));
            if (row.value("id").toString() == QString::number(source.id()))
                channel = row;
        }
        return channel;
    };
    const auto before = observed();
    ASSERT_FALSE(before.value("placement_token").toString().isEmpty());
    const auto copied = duplicate(*owner, hidden.id(), "hidden-copy");
    ASSERT_TRUE(copied.has_channel());
    ASSERT_TRUE(waitFor([&] {
        return observed().value("placement_token").toString()
            == QString::fromStdString(copied.channel().placement_token());
    }));
    const auto snapshot = daemon.call("state.snapshot");
    for (const auto& item : snapshot.result.value("channels").toArray())
        EXPECT_NE(item.toObject().value("id").toString(), QString::number(copied.channel().id()));
    proto::Envelope request;
    request.mutable_delete_channel()->set_channel_id(copied.channel().id());
    ASSERT_TRUE(owner->call(request)->has_ok());
    ASSERT_TRUE(waitFor([&] {
        return observed().value("placement_token").toString()
            != QString::fromStdString(copied.channel().placement_token());
    }));
    const auto after = observed();
    EXPECT_EQ(after.value("parent_id"), before.value("parent_id"));
    EXPECT_EQ(after.value("position"), before.value("position"));
}

TEST(DaemonPlacementRefresh, StructuralNotificationsRefreshTheWholeAuthorizedCachedList)
{
    TestServer server;
    TestDaemon daemon{"placement-refresh"};
    ASSERT_TRUE(server.start());
    ASSERT_TRUE(daemon.start());
    ASSERT_TRUE(daemon.registerOn(server, "placement-refresh", "test-password"));
    const auto createdServer = daemon.call("server.create", {{"name", "Placement refresh"}});
    ASSERT_TRUE(createdServer.ok);
    const QString sid = createdServer.result.value("id").toString();
    QString source;
    int initialCount = 0;
    ASSERT_TRUE(waitFor([&] {
        const auto snapshot = daemon.call("state.snapshot", {});
        if (!snapshot.ok)
            return false;
        const auto channels = snapshot.result.value("channels").toArray();
        initialCount = 0;
        for (const auto& item : channels) {
            const auto channel = item.toObject();
            if (channel.value("server_id").toString() == sid) {
                ++initialCount;
                if (channel.value("type").toString() == "text")
                    source = channel.value("id").toString();
            }
        }
        return !source.isEmpty();
    }));
    auto cachedListMatches = [&](const QString& token, int count, const QString& absent = {}) {
        const auto snapshot = daemon.call("state.snapshot", {});
        if (!snapshot.ok || token.isEmpty())
            return false;
        int found = 0;
        bool hasSource = false;
        for (const auto& item : snapshot.result.value("channels").toArray()) {
            const auto channel = item.toObject();
            if (channel.value("server_id").toString() != sid)
                continue;
            ++found;
            const QString id = channel.value("id").toString();
            if (id == absent || channel.value("placement_token").toString() != token)
                return false;
            hasSource = hasSource || id == source;
        }
        return found == count && hasSource;
    };
    const auto created = daemon.call("channel.create", {{"server", sid}, {"name", "new-sibling"}, {"type", "text"}});
    ASSERT_TRUE(created.ok);
    const QString createToken = created.result.value("placement_token").toString();
    ASSERT_TRUE(waitFor([&] { return cachedListMatches(createToken, initialCount + 1); }));
    const auto copied = daemon.call("channel.duplicate", {{"channel", created.result.value("id")}, {"name", "copy"}});
    ASSERT_TRUE(copied.ok);
    const QString copyToken = copied.result.value("placement_token").toString();
    EXPECT_NE(copyToken, createToken);
    ASSERT_TRUE(waitFor([&] { return cachedListMatches(copyToken, initialCount + 2); }));
    const QString copyId = copied.result.value("id").toString();
    ASSERT_TRUE(daemon.call("channel.delete", {{"channel", copyId}}).ok);
    RawClient owner(server);
    ASSERT_TRUE(owner.connect());
    ASSERT_TRUE(owner.hello());
    ASSERT_TRUE(owner.login("placement-refresh", "test-password"));
    proto::Envelope request;
    request.mutable_sync();
    const auto authoritative = owner.call(request);
    ASSERT_TRUE(authoritative && authoritative->has_sync_state());
    QString deleteToken;
    for (const auto& channel : authoritative->sync_state().channels())
        if (std::to_string(channel.id()) == source.toStdString())
            deleteToken = QString::fromStdString(channel.placement_token());
    EXPECT_NE(deleteToken, copyToken);
    ASSERT_TRUE(waitFor([&] { return cachedListMatches(deleteToken, initialCount + 1, copyId); }));
}
