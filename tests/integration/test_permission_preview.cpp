#include "Harness.hpp"
#include "omachat/core/Permissions.hpp"

#include <QJsonArray>
#include <gtest/gtest.h>

using namespace omachat;
using namespace omachat::test;

namespace {
class PermissionPreview : public ::testing::Test {
protected:
    TestServer server;
    std::unique_ptr<RawClient> owner, member, outsider;
    std::uint64_t serverId = 0, memberId = 0, outsiderId = 0;
    std::unique_ptr<RawClient> account(const std::string& name, std::uint64_t* id = nullptr)
    {
        auto client = std::make_unique<RawClient>(server);
        EXPECT_TRUE(client->connect());
        EXPECT_TRUE(client->hello());
        const auto auth = client->registerUser(name, "preview-password");
        EXPECT_TRUE(auth);
        if (auth && id)
            *id = auth->user().id();
        return client;
    }
    void SetUp() override
    {
        ASSERT_TRUE(server.start());
        owner = account("preview-owner");
        member = account("preview-member", &memberId);
        outsider = account("preview-outsider", &outsiderId);
        proto::Envelope request;
        request.mutable_create_server()->set_name("Permission sources");
        auto reply = owner->call(request);
        ASSERT_TRUE(reply && reply->has_server());
        serverId = reply->server().id();
        request.Clear();
        request.mutable_create_invite()->set_server_id(serverId);
        reply = owner->call(request);
        ASSERT_TRUE(reply && reply->has_invite());
        request.Clear();
        request.mutable_join_invite()->set_token(reply->invite().token());
        reply = member->call(request);
        ASSERT_TRUE(reply && reply->has_server());
    }
    std::uint64_t channel(proto::ChannelType type = proto::CHANNEL_TYPE_TEXT, std::uint64_t parent = 0)
    {
        proto::Envelope request;
        auto* create = request.mutable_create_channel();
        create->set_server_id(serverId);
        create->set_name(type == proto::CHANNEL_TYPE_CATEGORY ? "category" : "review");
        create->set_type(type);
        create->set_parent_id(parent);
        const auto reply = owner->call(request);
        EXPECT_TRUE(reply && reply->has_channel());
        return reply && reply->has_channel() ? reply->channel().id() : 0;
    }
    void overrideFor(std::uint64_t channelId, std::uint64_t target, permissions::Bits allow, permissions::Bits deny,
        bool role = false)
    {
        proto::Envelope request;
        auto* o = request.mutable_set_override()->mutable_override();
        o->set_channel_id(channelId);
        o->set_target_id(target);
        o->set_target_type(role ? proto::PermissionOverride::TARGET_ROLE : proto::PermissionOverride::TARGET_USER);
        o->set_allow(allow);
        o->set_deny(deny);
        const auto reply = owner->call(request);
        ASSERT_TRUE(reply && reply->has_ok());
    }
    std::uint64_t role(permissions::Bits bits)
    {
        proto::Envelope request;
        auto* create = request.mutable_create_role();
        create->set_server_id(serverId);
        create->set_name("Private role metadata");
        create->set_permissions(bits);
        auto reply = owner->call(request);
        EXPECT_TRUE(reply && reply->has_role());
        if (!reply || !reply->has_role())
            return 0;
        const auto id = reply->role().id();
        request.Clear();
        auto* assign = request.mutable_assign_role();
        assign->set_server_id(serverId);
        assign->set_user_id(memberId);
        assign->set_role_id(id);
        assign->set_add(true);
        reply = owner->call(request);
        EXPECT_TRUE(reply && reply->has_ok());
        return id;
    }
    proto::Envelope preview(RawClient& client, std::uint64_t channelId, std::uint64_t target = 0)
    {
        proto::Envelope request;
        request.mutable_permission_preview_request()->set_channel_id(channelId);
        request.mutable_permission_preview_request()->set_user_id(target);
        return client.call(request).value_or(proto::Envelope{});
    }
    proto::PermissionSource source(const proto::PermissionPreview& preview, permissions::Bits bit)
    {
        for (const auto& entry : preview.permissions())
            if (entry.permission() == bit)
                return entry;
        ADD_FAILURE() << "Missing source for " << permissions::name(bit);
        return {};
    }
    void parity(const proto::PermissionPreview& preview, RawClient& client)
    {
        proto::Envelope request;
        request.mutable_sync();
        const auto reply = client.call(request);
        ASSERT_TRUE(reply && reply->has_sync_state());
        bool found = false;
        for (const auto& c : reply->sync_state().channels()) {
            if (c.id() == preview.channel_id()) {
                EXPECT_EQ(c.effective_permissions(), preview.effective_permissions());
                found = true;
            }
        }
        EXPECT_TRUE(found);
        EXPECT_EQ(preview.permissions_size(), 21);
        permissions::Bits rebuilt = 0;
        for (const auto& entry : preview.permissions())
            if (entry.allowed())
                rebuilt |= entry.permission();
        EXPECT_EQ(rebuilt, preview.effective_permissions());
    }
};
} // namespace

TEST_F(PermissionPreview, SelfSourcesMatchAuthoritativeSyncAcrossInheritedLayersAndRoleConflicts)
{
    const auto parent = channel(proto::CHANNEL_TYPE_CATEGORY);
    const auto child = channel(proto::CHANNEL_TYPE_TEXT, parent);
    const auto first = role(permissions::ManageMessages);
    const auto second = role(0);
    overrideFor(parent, memberId, 0, permissions::SendMessages);
    overrideFor(child, first, 0, permissions::ManageMessages, true);
    overrideFor(child, second, permissions::ManageMessages, 0, true);
    auto reply = preview(*member, child);
    ASSERT_TRUE(reply.has_permission_preview());
    EXPECT_FALSE(reply.permission_preview().can_preview_members());
    EXPECT_EQ(reply.permission_preview().user_id(), memberId);
    EXPECT_EQ(source(reply.permission_preview(), permissions::SendMessages).source(), "category_user");
    EXPECT_FALSE(source(reply.permission_preview(), permissions::SendMessages).allowed());
    EXPECT_EQ(source(reply.permission_preview(), permissions::ManageMessages).source(), "channel_roles");
    EXPECT_TRUE(source(reply.permission_preview(), permissions::ManageMessages).allowed());
    parity(reply.permission_preview(), *member);
    overrideFor(child, memberId, permissions::SendMessages, permissions::ManageMessages);
    reply = preview(*member, child);
    ASSERT_TRUE(reply.has_permission_preview());
    EXPECT_EQ(source(reply.permission_preview(), permissions::SendMessages).source(), "channel_user");
    EXPECT_FALSE(source(reply.permission_preview(), permissions::ManageMessages).allowed());
    parity(reply.permission_preview(), *member);
}

TEST_F(PermissionPreview, RejectsOtherMembersOutsidersMissingAndHiddenChannelsWithoutMetadata)
{
    const auto child = channel();
    for (auto reply : {preview(*member, child, outsiderId), preview(*member, child, 1), preview(*outsider, child),
             preview(*owner, child, outsiderId), preview(*member, 1)}) {
        EXPECT_TRUE(reply.has_error());
        EXPECT_EQ(reply.error().code(), proto::ERROR_PERMISSION_DENIED);
        EXPECT_FALSE(reply.has_permission_preview());
    }
    overrideFor(child, memberId, 0, permissions::ViewChannel);
    auto reply = preview(*member, child);
    ASSERT_TRUE(reply.has_error());
    EXPECT_EQ(reply.error().code(), proto::ERROR_PERMISSION_DENIED);
    reply = preview(*owner, child, memberId);
    ASSERT_TRUE(reply.has_permission_preview());
    EXPECT_EQ(reply.permission_preview().effective_permissions(), 0u);
    EXPECT_EQ(source(reply.permission_preview(), permissions::ViewChannel).source(), "channel_user");
    EXPECT_EQ(source(reply.permission_preview(), permissions::SendMessages).source(), "view_channel_required");
    for (const auto& entry : reply.permission_preview().permissions())
        EXPECT_FALSE(entry.allowed());
}

TEST_F(PermissionPreview, ManagersMayPreviewMembersButMustStillSeeTheChannel)
{
    role(permissions::ManageRoles);
    const auto child = channel();
    auto reply = preview(*member, child);
    ASSERT_TRUE(reply.has_permission_preview());
    EXPECT_TRUE(reply.permission_preview().can_preview_members());
    proto::Envelope request;
    request.mutable_sync();
    const auto sync = owner->call(request);
    ASSERT_TRUE(sync && sync->has_sync_state());
    const auto ownerId = sync->sync_state().self().id();
    reply = preview(*member, child, ownerId);
    ASSERT_TRUE(reply.has_permission_preview());
    EXPECT_EQ(reply.permission_preview().effective_permissions(), permissions::kAll);
    EXPECT_EQ(source(reply.permission_preview(), permissions::SendMessages).source(), "owner");
    parity(reply.permission_preview(), *owner);
    overrideFor(child, memberId, 0, permissions::ViewChannel);
    reply = preview(*member, child, ownerId);
    ASSERT_TRUE(reply.has_error());
    EXPECT_EQ(reply.error().code(), proto::ERROR_PERMISSION_DENIED);
}

TEST_F(PermissionPreview, AdministratorBypassesOverridesAndReportsItsSource)
{
    role(permissions::Administrator);
    const auto child = channel();
    overrideFor(child, memberId, 0, permissions::ViewChannel | permissions::SendMessages);
    const auto reply = preview(*member, child);
    ASSERT_TRUE(reply.has_permission_preview());
    EXPECT_EQ(reply.permission_preview().effective_permissions(), permissions::kAll);
    EXPECT_EQ(source(reply.permission_preview(), permissions::SendMessages).source(), "administrator");
    parity(reply.permission_preview(), *member);
}

TEST_F(PermissionPreview, DaemonForwardsReadOnlyPreviewAndDenials)
{
    const auto child = channel();
    overrideFor(child, memberId, 0, permissions::SendMessages);
    TestDaemon daemon(QStringLiteral("permission-preview-ipc"));
    ASSERT_TRUE(daemon.start());
    ASSERT_TRUE(daemon.loginOn(server, QStringLiteral("preview-member"), QStringLiteral("preview-password")));
    const auto reply = daemon.call(QStringLiteral("permission.preview"), {{"channel", QString::number(child)}});
    ASSERT_TRUE(reply.ok) << reply.errorMessage.toStdString();
    EXPECT_EQ(reply.result.value("channel_id").toString(), QString::number(child));
    EXPECT_EQ(reply.result.value("user_id").toString(), QString::number(memberId));
    EXPECT_FALSE(reply.result.value("can_preview_members").toBool());
    const auto explicitSelf = daemon.call(QStringLiteral("permission.preview"),
        {{"channel", QString::number(child)}, {"user", QString::number(memberId)}});
    ASSERT_TRUE(explicitSelf.ok) << explicitSelf.errorMessage.toStdString();
    EXPECT_EQ(explicitSelf.result.value("user_id").toString(), QString::number(memberId));
    const auto entries = reply.result.value("permissions").toArray();
    EXPECT_EQ(entries.size(), 21);
    bool found = false;
    for (const auto& value : entries) {
        const auto entry = value.toObject();
        if (entry.value("name").toString() == QStringLiteral("SEND_MESSAGES")) {
            found = true;
            EXPECT_FALSE(entry.value("allowed").toBool());
            EXPECT_EQ(entry.value("source").toString(), QStringLiteral("channel_user"));
        }
    }
    EXPECT_TRUE(found);
    const auto denied = daemon.call(QStringLiteral("permission.preview"),
        {{"channel", QString::number(child)}, {"user", QString::number(outsiderId)}});
    EXPECT_FALSE(denied.ok);
    EXPECT_EQ(denied.errorCode, QStringLiteral("PermissionDenied"));
}
