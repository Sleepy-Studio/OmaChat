// Replayed message revisions must satisfy today's channel access, even when
// their historical audience included this account and a private mention mask.
#include "Harness.hpp"
#include "omachat/core/Permissions.hpp"
#include <gtest/gtest.h>

using namespace omachat;
using namespace omachat::test;

namespace {
class ResumeAccess : public ::testing::Test {
protected:
    TestServer server;
    std::unique_ptr<RawClient> owner, member;
    proto::AuthResult memberAuth;
    std::uint64_t channel = 0;
    std::uint64_t sequence = 0;
    std::uint64_t message = 0;

    proto::Envelope request(RawClient& client, proto::Envelope env)
    {
        auto reply = client.call(std::move(env));
        EXPECT_TRUE(reply);
        return reply.value_or(proto::Envelope{});
    }

    void SetUp() override
    {
        ASSERT_TRUE(server.start());
        owner = std::make_unique<RawClient>(server);
        member = std::make_unique<RawClient>(server);
        ASSERT_TRUE(owner->connect());
        ASSERT_TRUE(owner->hello());
        ASSERT_TRUE(owner->registerUser("resume-owner", "resume-access-password"));
        ASSERT_TRUE(member->connect());
        ASSERT_TRUE(member->hello());
        const auto auth = member->registerUser("resume-member", "resume-access-password");
        ASSERT_TRUE(auth);
        memberAuth = *auth;
        proto::Envelope env;
        env.mutable_create_server()->set_name("Resume access lab");
        const auto community = request(*owner, env);
        ASSERT_TRUE(community.has_server());
        env.Clear();
        env.mutable_create_invite()->set_server_id(community.server().id());
        const auto invite = request(*owner, env);
        ASSERT_TRUE(invite.has_invite());
        env.Clear();
        env.mutable_join_invite()->set_token(invite.invite().token());
        ASSERT_FALSE(request(*member, env).has_error());
        env.Clear();
        env.mutable_sync();
        const auto synced = request(*member, env);
        ASSERT_TRUE(synced.has_sync_state());
        sequence = synced.sync_state().last_sequence();
        for (const auto& room : synced.sync_state().channels())
            if (room.type() == proto::CHANNEL_TYPE_TEXT && room.server_id() == community.server().id())
                channel = room.id();
        ASSERT_NE(channel, 0u);
        member->abort();
        waitFor([] { return false; }, 20);
        env.Clear();
        env.mutable_send_message()->set_channel_id(channel);
        env.mutable_send_message()->set_content("private original @{everyone}");
        const auto created = request(*owner, env);
        ASSERT_TRUE(created.has_chat_message());
        message = created.chat_message().id();
        env.Clear();
        env.mutable_edit_message()->set_message_id(message);
        env.mutable_edit_message()->set_content("private edited @{everyone}");
        ASSERT_TRUE(request(*owner, env).has_chat_message());
    }

    void revoke(permissions::Bits denied)
    {
        proto::Envelope env;
        auto* rule = env.mutable_set_override()->mutable_override();
        rule->set_channel_id(channel);
        rule->set_target_type(proto::PermissionOverride::TARGET_USER);
        rule->set_target_id(memberAuth.user().id());
        rule->set_deny(denied);
        ASSERT_FALSE(request(*owner, env).has_error());
    }

    proto::Envelope resume(RawClient& back)
    {
        EXPECT_TRUE(back.connect());
        EXPECT_TRUE(back.hello());
        proto::Envelope env;
        env.mutable_resume()->set_access_token(memberAuth.access_token());
        env.mutable_resume()->set_session_id(memberAuth.session_id());
        env.mutable_resume()->set_last_sequence(sequence);
        return request(back, env);
    }

    void expectFullSyncWithoutMessages()
    {
        RawClient back(server);
        const auto reply = resume(back);
        ASSERT_TRUE(reply.has_error());
        EXPECT_EQ(reply.error().code(), proto::ERROR_RESUME_FAILED);
        EXPECT_FALSE(reply.has_resume_result());
        EXPECT_FALSE(back.waitEvent(
            [](const proto::Event& e) { return e.has_message_create() || e.has_message_update(); }, 100));
        // Authentication is retained so the daemon can request a safe full sync.
        proto::Envelope sync;
        sync.mutable_sync();
        EXPECT_TRUE(request(back, sync).has_sync_state());
    }
};
} // namespace

TEST_F(ResumeAccess, LostViewChannelForcesFullSyncWithoutLeakingMessageRevisions)
{
    revoke(permissions::ViewChannel);
    expectFullSyncWithoutMessages();
}

TEST_F(ResumeAccess, LostReadHistoryForcesFullSyncWithoutLeakingMessageRevisions)
{
    revoke(permissions::ReadHistory);
    expectFullSyncWithoutMessages();
}

TEST_F(ResumeAccess, StillAuthorizedReplaysOriginalAndEditedPrivateMasks)
{
    RawClient back(server);
    const auto reply = resume(back);
    ASSERT_TRUE(reply.has_resume_result());
    const auto created = back.waitEvent(
        [this](const proto::Event& e) { return e.has_message_create() && e.message_create().id() == message; });
    ASSERT_TRUE(created);
    EXPECT_EQ(created->message_create().content(), "private original @{everyone}");
    EXPECT_EQ(created->message_create().mention_classification().reason_mask(), 4u);
    const auto edited = back.waitEvent(
        [this](const proto::Event& e) { return e.has_message_update() && e.message_update().id() == message; });
    ASSERT_TRUE(edited);
    EXPECT_EQ(edited->message_update().content(), "private edited @{everyone}");
    EXPECT_EQ(edited->message_update().mention_classification().reason_mask(), 4u);
}
