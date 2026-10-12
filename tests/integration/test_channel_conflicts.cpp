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
class ChannelConflicts : public ::testing::Test {
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
    std::uint64_t uploadImage(std::uint64_t channel, Qt::GlobalColor color)
    {
        QByteArray png;
        QBuffer buffer(&png);
        EXPECT_TRUE(buffer.open(QIODevice::WriteOnly));
        QImage image(8, 8, QImage::Format_ARGB32);
        image.fill(color);
        EXPECT_TRUE(image.save(&buffer, "PNG"));
        proto::Envelope request;
        auto* upload = request.mutable_begin_upload();
        upload->set_channel_id(channel);
        upload->set_filename("image.png");
        upload->set_mime_type("image/png");
        upload->set_size(static_cast<std::uint64_t>(png.size()));
        upload->set_channel_artwork(true);
        const auto ticket = owner->call(request);
        EXPECT_TRUE(ticket && ticket->has_upload_ticket());
        if (!ticket || !ticket->has_upload_ticket())
            return 0;
        const auto id = ticket->upload_ticket().attachment_id();
        request.Clear();
        request.mutable_upload_chunk()->set_attachment_id(id);
        request.mutable_upload_chunk()->set_data(png.toStdString());
        EXPECT_TRUE(owner->call(request)->has_ok());
        request.Clear();
        request.mutable_finish_upload()->set_attachment_id(id);
        request.mutable_finish_upload()->set_sha256(
            QCryptographicHash::hash(png, QCryptographicHash::Sha256).toStdString());
        EXPECT_TRUE(owner->call(request)->has_attachment());
        return id;
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

TEST_F(ChannelConflicts, StaleArtworkRejectsReplacementAndRemovalWithoutConsumingUpload)
{
    const auto channel = create("images", proto::CHANNEL_TYPE_TEXT);
    const auto first = uploadImage(channel.id(), Qt::blue);
    const auto retained = uploadImage(channel.id(), Qt::green);
    ASSERT_NE(first, 0u);
    ASSERT_NE(retained, 0u);
    proto::Envelope request;
    auto* update = request.mutable_update_channel();
    update->set_channel_id(channel.id());
    update->set_set_icon(true);
    update->set_icon_attachment_id(first);
    ASSERT_TRUE(owner->call(request)->has_channel());
    update->set_check_artwork(true);
    update->set_expected_icon_attachment_id(0);
    update->set_expected_banner_attachment_id(0);
    update->set_icon_attachment_id(retained);
    auto conflict = owner->call(request);
    ASSERT_TRUE(conflict && conflict->has_error());
    EXPECT_EQ(conflict->error().code(), proto::ERROR_CONFLICT);
    EXPECT_EQ(find(sync(*owner), channel.id()).icon_attachment_id(), first);
    update->set_icon_attachment_id(0);
    conflict = owner->call(request);
    ASSERT_TRUE(conflict && conflict->has_error());
    EXPECT_EQ(conflict->error().code(), proto::ERROR_CONFLICT);
    EXPECT_EQ(find(sync(*owner), channel.id()).icon_attachment_id(), first);
    // Review latest originals and retry the same uploaded asset successfully.
    update->set_expected_icon_attachment_id(first);
    update->set_icon_attachment_id(retained);
    const auto applied = owner->call(request);
    ASSERT_TRUE(applied && applied->has_channel());
    EXPECT_EQ(applied->channel().icon_attachment_id(), retained);
    // Both references are compared, even when changing only the icon.
    update->set_expected_icon_attachment_id(retained);
    update->set_expected_banner_attachment_id(99999);
    update->set_icon_attachment_id(0);
    conflict = owner->call(request);
    ASSERT_TRUE(conflict && conflict->has_error());
    EXPECT_EQ(conflict->error().code(), proto::ERROR_CONFLICT);
}

TEST_F(ChannelConflicts, StalePlacementRejectsMoveWithoutChangingAnySibling)
{
    const auto category = create("Destination", proto::CHANNEL_TYPE_CATEGORY);
    const auto channel = create("source", proto::CHANNEL_TYPE_TEXT);
    const auto sibling = create("sibling", proto::CHANNEL_TYPE_TEXT);
    proto::Envelope request;
    auto* update = request.mutable_update_channel();
    update->set_channel_id(channel.id());
    update->set_set_parent(true);
    update->set_parent_id(category.id());
    update->set_set_before(true);
    update->set_before_id(0);
    ASSERT_TRUE(owner->call(request)->has_channel());
    const auto before = sync(*owner);
    update->set_check_placement(true);
    update->set_expected_parent_id(channel.parent_id());
    update->set_expected_position(channel.position());
    update->set_parent_id(0);
    update->set_before_id(sibling.id());
    const auto conflict = owner->call(request);
    ASSERT_TRUE(conflict && conflict->has_error());
    EXPECT_EQ(conflict->error().code(), proto::ERROR_CONFLICT);
    const auto after = sync(*owner);
    for (const auto& c : before.channels()) {
        EXPECT_EQ(find(after, c.id()).parent_id(), c.parent_id());
        EXPECT_EQ(find(after, c.id()).position(), c.position());
    }
    const auto latest = find(after, channel.id());
    update->set_expected_parent_id(latest.parent_id());
    update->set_expected_position(latest.position());
    const auto applied = owner->call(request);
    ASSERT_TRUE(applied && applied->has_channel());
    EXPECT_EQ(applied->channel().parent_id(), 0u);
}

TEST_F(ChannelConflicts, CheckedScopesRejectMixedEditsAndAuthorizeBeforeComparing)
{
    const auto channel = create("source", proto::CHANNEL_TYPE_TEXT);
    proto::Envelope request;
    auto* update = request.mutable_update_channel();
    update->set_channel_id(channel.id());
    update->set_check_artwork(true);
    update->set_expected_icon_attachment_id(99999);
    update->set_set_icon(true);
    const auto denied = member->call(request);
    ASSERT_TRUE(denied && denied->has_error());
    EXPECT_EQ(denied->error().code(), proto::ERROR_PERMISSION_DENIED);
    update->set_name("mixed");
    const auto invalid = owner->call(request);
    ASSERT_TRUE(invalid && invalid->has_error());
    EXPECT_EQ(invalid->error().code(), proto::ERROR_BAD_REQUEST);
    update->set_name("");
    update->set_check_placement(true);
    update->set_set_parent(true);
    const auto mixed = owner->call(request);
    ASSERT_TRUE(mixed && mixed->has_error());
    EXPECT_EQ(mixed->error().code(), proto::ERROR_BAD_REQUEST);
    EXPECT_EQ(find(sync(*owner), channel.id()).name(), channel.name());
}
