#include "Harness.hpp"
#include "omachat/core/Permissions.hpp"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <algorithm>
#include <gtest/gtest.h>

using namespace omachat;
using namespace omachat::test;

namespace {
class CategoryDeletion : public ::testing::Test {
protected:
    TestServer server;
    std::unique_ptr<RawClient> owner, member;
    std::uint64_t serverId = 0, memberId = 0;

    std::unique_ptr<RawClient> connect(const std::string& name, bool existing = false)
    {
        auto c = std::make_unique<RawClient>(server);
        EXPECT_TRUE(c->connect());
        EXPECT_TRUE(c->hello());
        const auto auth = existing ? c->login(name, "category-password") : c->registerUser(name, "category-password");
        EXPECT_TRUE(auth.has_value());
        return c;
    }
    void SetUp() override
    {
        ASSERT_TRUE(server.start());
        owner = connect("category-owner");
        member = connect("category-member");
        proto::Envelope request;
        request.mutable_create_server()->set_name("Category deletion");
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
            if (user.username() == "category-member")
                memberId = user.id();
        ASSERT_NE(memberId, 0u);
    }
    proto::SyncState sync(RawClient& c)
    {
        proto::Envelope request;
        request.mutable_sync();
        const auto reply = c.call(request);
        EXPECT_TRUE(reply && reply->has_sync_state());
        return reply && reply->has_sync_state() ? reply->sync_state() : proto::SyncState{};
    }
    proto::Channel create(const std::string& name, proto::ChannelType type, std::uint64_t parent = 0)
    {
        proto::Envelope request;
        auto* channel = request.mutable_create_channel();
        channel->set_server_id(serverId);
        channel->set_name(name);
        channel->set_type(type);
        channel->set_parent_id(parent);
        const auto reply = owner->call(request);
        EXPECT_TRUE(reply && reply->has_channel());
        return reply && reply->has_channel() ? reply->channel() : proto::Channel{};
    }
    proto::Envelope remove(RawClient& c, std::uint64_t id)
    {
        proto::Envelope request;
        request.mutable_delete_channel()->set_channel_id(id);
        return c.call(request).value_or(proto::Envelope{});
    }
    void overrideFor(std::uint64_t channel, std::uint64_t allow, std::uint64_t deny)
    {
        proto::Envelope request;
        auto* override = request.mutable_set_override()->mutable_override();
        override->set_channel_id(channel);
        override->set_target_type(proto::PermissionOverride::TARGET_USER);
        override->set_target_id(memberId);
        override->set_allow(allow);
        override->set_deny(deny);
        const auto reply = owner->call(request);
        ASSERT_TRUE(reply && reply->has_ok());
    }
    static proto::Channel find(const proto::SyncState& state, std::uint64_t id)
    {
        for (const auto& channel : state.channels())
            if (channel.id() == id)
                return channel;
        return {};
    }
};
} // namespace

TEST_F(CategoryDeletion, RetainsChildrenMessagesOverridesAndOrderAcrossRestart)
{
    const auto category = create("Temporary", proto::CHANNEL_TYPE_CATEGORY);
    const auto first = create("first-child", proto::CHANNEL_TYPE_TEXT, category.id());
    const auto second = create("second-child", proto::CHANNEL_TYPE_TEXT, category.id());
    const auto top = create("existing-top", proto::CHANNEL_TYPE_TEXT);
    overrideFor(second.id(), 0, permissions::SendMessages);
    proto::Envelope request;
    request.mutable_send_message()->set_channel_id(first.id());
    request.mutable_send_message()->set_content("Retained when category goes away");
    const auto sent = owner->call(request);
    ASSERT_TRUE(sent && sent->has_chat_message());
    const auto messageId = sent->chat_message().id();
    member->clearEvents();
    ASSERT_TRUE(remove(*owner, category.id()).has_ok());
    EXPECT_TRUE(member->waitEvent([&](const proto::Event& e) {
        return e.has_channel_delete() && e.channel_delete().channel_id() == category.id();
    }));
    EXPECT_TRUE(member->waitEvent([&](const proto::Event& e) {
        return e.has_permissions_changed() && e.permissions_changed().server_id() == serverId;
    }));
    const auto check = [&](RawClient& c) {
        const auto state = sync(c);
        EXPECT_EQ(find(state, category.id()).id(), 0u);
        EXPECT_EQ(find(state, first.id()).parent_id(), 0u);
        EXPECT_EQ(find(state, second.id()).parent_id(), 0u);
        EXPECT_GT(find(state, first.id()).position(), find(state, top.id()).position());
        EXPECT_EQ(find(state, second.id()).position(), find(state, first.id()).position() + 1);
        std::vector<std::uint32_t> positions;
        for (const auto& channel : state.channels())
            if (channel.server_id() == serverId && channel.parent_id() == 0
                && channel.type() != proto::CHANNEL_TYPE_CATEGORY)
                positions.push_back(channel.position());
        std::sort(positions.begin(), positions.end());
        for (size_t i = 0; i < positions.size(); ++i)
            EXPECT_EQ(positions[i], i);
        request.Clear();
        request.mutable_get_messages()->set_channel_id(first.id());
        const auto history = c.call(request);
        ASSERT_TRUE(history && history->has_message_page());
        ASSERT_EQ(history->message_page().messages_size(), 1);
        EXPECT_EQ(history->message_page().messages(0).id(), messageId);
    };
    check(*owner);
    check(*member);
    owner.reset();
    member.reset();
    server.stop();
    ASSERT_TRUE(server.start());
    owner = connect("category-owner", true);
    member = connect("category-member", true);
    check(*owner);
    check(*member);
    request.Clear();
    request.mutable_send_message()->set_channel_id(second.id());
    request.mutable_send_message()->set_content("Still forbidden");
    const auto denied = member->call(request);
    ASSERT_TRUE(denied && denied->has_error());
    EXPECT_EQ(denied->error().code(), proto::ERROR_PERMISSION_DENIED);
}

TEST_F(CategoryDeletion, RejectsInheritedOverridesWithoutExposingChildren)
{
    const auto category = create("Private", proto::CHANNEL_TYPE_CATEGORY);
    const auto child = create("private-child", proto::CHANNEL_TYPE_TEXT, category.id());
    overrideFor(category.id(), 0, permissions::ViewChannel);
    EXPECT_EQ(find(sync(*member), child.id()).id(), 0u);
    const auto denied = remove(*owner, category.id());
    ASSERT_TRUE(denied.has_error());
    EXPECT_EQ(denied.error().code(), proto::ERROR_BAD_REQUEST);
    EXPECT_NE(denied.error().message().find("inherited permission overrides"), std::string::npos);
    EXPECT_EQ(find(sync(*owner), child.id()).parent_id(), category.id());
    EXPECT_EQ(find(sync(*owner), category.id()).id(), category.id());
    EXPECT_EQ(find(sync(*member), child.id()).id(), 0u);
}

TEST_F(CategoryDeletion, RejectsMembersAndManagersWhoCannotManageEveryChild)
{
    const auto category = create("Managed", proto::CHANNEL_TYPE_CATEGORY);
    const auto child = create("protected-child", proto::CHANNEL_TYPE_TEXT, category.id());
    auto denied = remove(*member, category.id());
    ASSERT_TRUE(denied.has_error());
    EXPECT_EQ(denied.error().code(), proto::ERROR_PERMISSION_DENIED);
    overrideFor(category.id(), permissions::ManageChannel, 0);
    overrideFor(child.id(), 0, permissions::ManageChannel);
    denied = remove(*member, category.id());
    ASSERT_TRUE(denied.has_error());
    EXPECT_EQ(denied.error().code(), proto::ERROR_PERMISSION_DENIED);
    EXPECT_NE(denied.error().message().find("every channel"), std::string::npos);
    EXPECT_EQ(find(sync(*owner), child.id()).parent_id(), category.id());
    EXPECT_EQ(find(sync(*owner), category.id()).id(), category.id());
}

TEST_F(CategoryDeletion, RollsBackReparentingWhenCategoryDeletionFails)
{
    const auto category = create("Rollback", proto::CHANNEL_TYPE_CATEGORY);
    const auto child = create("rollback-child", proto::CHANNEL_TYPE_TEXT, category.id());
    const auto top = create("rollback-top", proto::CHANNEL_TYPE_TEXT);
    // The harness owns a single real server SQLite connection. A trigger
    // rejects the final DELETE after the handler has already moved children.
    QString connection;
    for (const auto& name : QSqlDatabase::connectionNames()) {
        if (QSqlDatabase::database(name).databaseName().endsWith("/server.db")) {
            ASSERT_TRUE(connection.isEmpty());
            connection = name;
        }
    }
    ASSERT_FALSE(connection.isEmpty());
    auto db = QSqlDatabase::database(connection);
    QSqlQuery query(db);
    ASSERT_TRUE(query.exec(QStringLiteral("CREATE TRIGGER reject_category_delete BEFORE DELETE ON channels "
                                          "WHEN OLD.id = %1 BEGIN SELECT RAISE(ABORT, 'test category failure'); END")
            .arg(category.id())));
    owner->clearEvents();
    member->clearEvents();
    const auto failed = remove(*owner, category.id());
    ASSERT_TRUE(failed.has_error());
    EXPECT_EQ(failed.error().code(), proto::ERROR_INTERNAL);
    const auto state = sync(*owner);
    EXPECT_EQ(find(state, category.id()).id(), category.id());
    EXPECT_EQ(find(state, child.id()).parent_id(), category.id());
    EXPECT_EQ(find(state, child.id()).position(), child.position());
    EXPECT_EQ(find(state, top.id()).position(), top.position());
    ASSERT_TRUE(query.exec(QStringLiteral("SELECT parent_id, position FROM channels WHERE id = %1").arg(child.id())));
    ASSERT_TRUE(query.next());
    EXPECT_EQ(query.value(0).toULongLong(), category.id());
    EXPECT_EQ(query.value(1).toUInt(), child.position());
    query.finish();
    EXPECT_FALSE(member->waitEvent(
        [&](const proto::Event& e) {
            return e.has_channel_delete() && e.channel_delete().channel_id() == category.id();
        },
        100));
    EXPECT_FALSE(member->waitEvent(
        [&](const proto::Event& e) {
            return e.has_permissions_changed() && e.permissions_changed().server_id() == serverId;
        },
        100));
    ASSERT_TRUE(query.exec(QStringLiteral("DROP TRIGGER reject_category_delete")));
    EXPECT_TRUE(remove(*owner, category.id()).has_ok());
    EXPECT_EQ(find(sync(*owner), child.id()).parent_id(), 0u);
}
