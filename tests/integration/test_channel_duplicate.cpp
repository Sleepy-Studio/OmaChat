#include "Harness.hpp"
#include "omachat/core/Permissions.hpp"

#include <QBuffer>
#include <QCryptographicHash>
#include <QImage>
#include <algorithm>
#include <gtest/gtest.h>

using namespace omachat;
using namespace omachat::test;

namespace {
class ChannelDuplicate : public ::testing::Test {
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

TEST_F(ChannelDuplicate, CopiesRestrictedConfigurationBeforeAnyObserverCanSeeIt)
{
    const auto category = create("Private category", proto::CHANNEL_TYPE_CATEGORY);
    const auto source = create("source", proto::CHANNEL_TYPE_TEXT, category.id());
    overrideFor(source.id(), 0, permissions::ViewChannel);
    proto::Envelope update;
    auto* details = update.mutable_update_channel();
    details->set_channel_id(source.id());
    details->set_set_topic(true);
    details->set_topic("Private topic");
    details->set_set_description(true);
    details->set_description("Private description");
    ASSERT_TRUE(owner->call(update)->has_channel());
    proto::Envelope message;
    message.mutable_send_message()->set_channel_id(source.id());
    message.mutable_send_message()->set_content("Do not copy this history");
    ASSERT_TRUE(owner->call(message)->has_chat_message());
    member->clearEvents();
    const auto result = duplicate(*owner, source.id(), "Copy With Spaces");
    ASSERT_TRUE(result.has_channel()) << result.error().message();
    const auto& copy = result.channel();
    EXPECT_NE(copy.id(), source.id());
    EXPECT_EQ(copy.name(), "copy-with-spaces");
    EXPECT_EQ(copy.parent_id(), category.id());
    EXPECT_EQ(copy.topic(), "Private topic");
    EXPECT_EQ(copy.description(), "Private description");
    EXPECT_GT(copy.position(), source.position());
    EXPECT_EQ(find(sync(*member), copy.id()).id(), 0u);
    EXPECT_FALSE(member->waitEvent(
        [&](const auto& e) { return e.has_channel_create() && e.channel_create().id() == copy.id(); }, 100));
    proto::Envelope history;
    history.mutable_get_messages()->set_channel_id(copy.id());
    const auto empty = owner->call(history);
    ASSERT_TRUE(empty && empty->has_message_page());
    EXPECT_EQ(empty->message_page().messages_size(), 0);
    proto::Envelope overrides;
    overrides.mutable_list_overrides()->set_channel_id(copy.id());
    const auto copiedOverrides = owner->call(overrides);
    ASSERT_TRUE(copiedOverrides && copiedOverrides->has_override_list());
    ASSERT_EQ(copiedOverrides->override_list().overrides_size(), 1);
    EXPECT_EQ(copiedOverrides->override_list().overrides(0).deny(), permissions::ViewChannel);
}

TEST_F(ChannelDuplicate, CategoryCopiesHaveNoChildrenAndRemainHiddenInSnapshots)
{
    const auto category = create("Secret", proto::CHANNEL_TYPE_CATEGORY);
    const auto child = create("child", proto::CHANNEL_TYPE_VOICE, category.id());
    overrideFor(category.id(), 0, permissions::ViewChannel);
    member->clearEvents();
    const auto result = duplicate(*owner, category.id(), "Secret copy");
    ASSERT_TRUE(result.has_channel());
    EXPECT_EQ(result.channel().type(), proto::CHANNEL_TYPE_CATEGORY);
    EXPECT_EQ(result.channel().parent_id(), 0u);
    const auto state = sync(*owner);
    for (const auto& channel : state.channels())
        EXPECT_NE(channel.parent_id(), result.channel().id());
    EXPECT_EQ(find(state, child.id()).parent_id(), category.id());
    EXPECT_EQ(find(sync(*member), result.channel().id()).id(), 0u);
    EXPECT_FALSE(member->waitEvent(
        [&](const auto& e) { return e.has_channel_create() && e.channel_create().id() == result.channel().id(); },
        100));
    member->clearEvents();
    proto::Envelope rename;
    rename.mutable_update_channel()->set_channel_id(result.channel().id());
    rename.mutable_update_channel()->set_name("Still secret");
    ASSERT_TRUE(owner->call(rename)->has_channel());
    EXPECT_FALSE(member->waitEvent(
        [&](const auto& e) { return e.has_channel_update() && e.channel_update().id() == result.channel().id(); },
        100));
    EXPECT_EQ(find(sync(*member), result.channel().id()).id(), 0u);
    const auto nested = duplicate(*owner, category.id(), "Nested", category.id());
    ASSERT_TRUE(nested.has_error());
    EXPECT_EQ(nested.error().code(), proto::ERROR_BAD_REQUEST);
}

TEST_F(ChannelDuplicate, RejectsInvalidDestinationsNamesAndUnauthorizedCallersWithoutMutation)
{
    const auto source = create("source", proto::CHANNEL_TYPE_VOICE);
    const auto category = create("Destination", proto::CHANNEL_TYPE_CATEGORY);
    const int count = sync(*owner).channels_size();
    auto denied = duplicate(*member, source.id());
    ASSERT_TRUE(denied.has_error());
    EXPECT_EQ(denied.error().code(), proto::ERROR_PERMISSION_DENIED);
    // Managing a source alone is insufficient to create a new server channel.
    overrideFor(source.id(), permissions::ManageChannel, 0);
    denied = duplicate(*member, source.id());
    ASSERT_TRUE(denied.has_error());
    EXPECT_EQ(denied.error().code(), proto::ERROR_PERMISSION_DENIED);
    for (const auto parent : {source.id(), std::uint64_t(999999999)}) {
        const auto result = duplicate(*owner, source.id(), "copy", parent);
        ASSERT_TRUE(result.has_error());
        EXPECT_EQ(result.error().code(), proto::ERROR_BAD_REQUEST);
    }
    for (const auto& name : {std::string(), std::string(65, 'x')})
        EXPECT_TRUE(duplicate(*owner, source.id(), name).has_error());
    EXPECT_EQ(sync(*owner).channels_size(), count);
    const auto copy = duplicate(*owner, source.id(), "Voice Copy", category.id());
    ASSERT_TRUE(copy.has_channel());
    EXPECT_EQ(copy.channel().type(), proto::CHANNEL_TYPE_VOICE);
    EXPECT_EQ(copy.channel().parent_id(), category.id());
    EXPECT_EQ(copy.channel().name(), "Voice Copy");
    const auto root = duplicate(*owner, copy.channel().id(), "Root copy", 0);
    ASSERT_TRUE(root.has_channel());
    EXPECT_EQ(root.channel().parent_id(), 0u);
    overrideFor(source.id(), 0, permissions::ViewChannel);
    denied = duplicate(*member, source.id());
    ASSERT_TRUE(denied.has_error());
    EXPECT_EQ(denied.error().code(), proto::ERROR_NOT_FOUND);
}

TEST_F(ChannelDuplicate, SharedArtworkAuthorizesAnyVisibleReferenceAndSurvivesSourceDeletion)
{
    const auto source = create("art-source", proto::CHANNEL_TYPE_TEXT);
    QByteArray png;
    QBuffer buffer(&png);
    ASSERT_TRUE(buffer.open(QIODevice::WriteOnly));
    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    ASSERT_TRUE(image.save(&buffer, "PNG"));
    proto::Envelope request;
    auto* upload = request.mutable_begin_upload();
    upload->set_channel_id(source.id());
    upload->set_filename("icon.png");
    upload->set_mime_type("image/png");
    upload->set_size(static_cast<std::uint64_t>(png.size()));
    upload->set_channel_artwork(true);
    const auto ticket = owner->call(request);
    ASSERT_TRUE(ticket && ticket->has_upload_ticket());
    const auto assetId = ticket->upload_ticket().attachment_id();
    request.Clear();
    request.mutable_upload_chunk()->set_attachment_id(assetId);
    request.mutable_upload_chunk()->set_data(png.toStdString());
    ASSERT_TRUE(owner->call(request)->has_ok());
    request.Clear();
    request.mutable_finish_upload()->set_attachment_id(assetId);
    request.mutable_finish_upload()->set_sha256(
        QCryptographicHash::hash(png, QCryptographicHash::Sha256).toStdString());
    ASSERT_TRUE(owner->call(request)->has_attachment());
    request.Clear();
    request.mutable_update_channel()->set_channel_id(source.id());
    request.mutable_update_channel()->set_set_icon(true);
    request.mutable_update_channel()->set_icon_attachment_id(assetId);
    request.mutable_update_channel()->set_set_banner(true);
    request.mutable_update_channel()->set_banner_attachment_id(assetId);
    ASSERT_TRUE(owner->call(request)->has_channel());
    const auto result = duplicate(*owner, source.id());
    ASSERT_TRUE(result.has_channel());
    EXPECT_EQ(result.channel().icon_attachment_id(), assetId);
    EXPECT_EQ(result.channel().banner_attachment_id(), assetId);
    overrideFor(source.id(), 0, permissions::ViewChannel);
    request.Clear();
    request.mutable_download()->set_attachment_id(assetId);
    ASSERT_TRUE(member->call(request)->has_file_chunk());
    overrideFor(result.channel().id(), 0, permissions::ViewChannel);
    const auto hidden = member->call(request);
    ASSERT_TRUE(hidden && hidden->has_error());
    EXPECT_EQ(hidden->error().code(), proto::ERROR_NOT_FOUND);
    overrideFor(result.channel().id(), permissions::ViewChannel, 0);
    proto::Envelope remove;
    remove.mutable_delete_channel()->set_channel_id(source.id());
    ASSERT_TRUE(owner->call(remove)->has_ok());
    ASSERT_TRUE(member->call(request)->has_file_chunk());
    EXPECT_EQ(find(sync(*owner), result.channel().id()).icon_attachment_id(), assetId);
    owner.reset();
    member.reset();
    server.stop();
    ASSERT_TRUE(server.start());
    member = std::make_unique<RawClient>(server);
    ASSERT_TRUE(member->connect());
    ASSERT_TRUE(member->hello());
    ASSERT_TRUE(member->login("duplicate-member", "duplicate-password"));
    EXPECT_EQ(find(sync(*member), result.channel().id()).icon_attachment_id(), assetId);
    ASSERT_TRUE(member->call(request)->has_file_chunk());
}

TEST_F(ChannelDuplicate, VisibleChildOfHiddenCategorySurvivesSnapshotWithoutParentMetadata)
{
    const auto category = create("Hidden category", proto::CHANNEL_TYPE_CATEGORY);
    const auto child = create("visible-child", proto::CHANNEL_TYPE_TEXT, category.id());
    overrideFor(category.id(), 0, permissions::ViewChannel);
    overrideFor(child.id(), permissions::ViewChannel, 0);
    const auto state = sync(*member);
    EXPECT_EQ(find(state, category.id()).id(), 0u);
    EXPECT_EQ(find(state, child.id()).id(), child.id());
}

TEST_F(ChannelDuplicate, RejectsCrossServerConversationAndUnmanageableDestination)
{
    const auto source = create("source", proto::CHANNEL_TYPE_TEXT);
    const auto destination = create("Destination", proto::CHANNEL_TYPE_CATEGORY);
    proto::Envelope request;
    request.mutable_create_role()->set_server_id(serverId);
    request.mutable_create_role()->set_name("Channel creator");
    request.mutable_create_role()->set_permissions(permissions::CreateChannel);
    auto result = owner->call(request);
    ASSERT_TRUE(result && result->has_role());
    const auto roleId = result->role().id();
    request.Clear();
    request.mutable_assign_role()->set_server_id(serverId);
    request.mutable_assign_role()->set_user_id(memberId);
    request.mutable_assign_role()->set_role_id(roleId);
    request.mutable_assign_role()->set_add(true);
    ASSERT_TRUE(owner->call(request)->has_ok());
    overrideFor(source.id(), permissions::ManageChannel, 0);
    auto rejected = duplicate(*member, source.id(), "copy", destination.id());
    ASSERT_TRUE(rejected.has_error());
    EXPECT_EQ(rejected.error().code(), proto::ERROR_PERMISSION_DENIED);
    overrideFor(destination.id(), permissions::ManageChannel, 0);
    ASSERT_TRUE(duplicate(*member, source.id(), "permitted", destination.id()).has_channel());
    overrideFor(destination.id(), permissions::ManageChannel, permissions::ViewChannel);
    rejected = duplicate(*member, source.id(), "hidden", destination.id());
    ASSERT_TRUE(rejected.has_error());
    EXPECT_EQ(rejected.error().code(), proto::ERROR_PERMISSION_DENIED);

    request.Clear();
    request.mutable_create_server()->set_name("Other server");
    result = owner->call(request);
    ASSERT_TRUE(result && result->has_server());
    const auto otherServer = result->server().id();
    request.Clear();
    request.mutable_create_channel()->set_server_id(otherServer);
    request.mutable_create_channel()->set_name("Other category");
    request.mutable_create_channel()->set_type(proto::CHANNEL_TYPE_CATEGORY);
    result = owner->call(request);
    ASSERT_TRUE(result && result->has_channel());
    rejected = duplicate(*owner, source.id(), "cross-server", result->channel().id());
    ASSERT_TRUE(rejected.has_error());
    EXPECT_EQ(rejected.error().code(), proto::ERROR_BAD_REQUEST);
    request.Clear();
    request.mutable_open_dm()->set_user_id(memberId);
    result = owner->call(request);
    ASSERT_TRUE(result && result->has_channel());
    rejected = duplicate(*owner, result->channel().id());
    ASSERT_TRUE(rejected.has_error());
    EXPECT_EQ(rejected.error().code(), proto::ERROR_BAD_REQUEST);
}
