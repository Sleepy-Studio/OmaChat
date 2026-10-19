#include "Harness.hpp"
#include "omachat/core/Permissions.hpp"
#include <QJsonArray>
#include <gtest/gtest.h>

using namespace omachat;
using namespace omachat::test;

namespace {
class MentionProtocol : public ::testing::Test {
protected:
    TestServer server;
    std::unique_ptr<RawClient> owner, member, peer;
    proto::AuthResult memberAuth;
    std::uint64_t community = 0, channel = 0, roleId = 0, memberId = 0;
    std::unique_ptr<RawClient> account(const std::string& name, proto::AuthResult* result = nullptr)
    {
        auto client = std::make_unique<RawClient>(server);
        EXPECT_TRUE(client->connect());
        EXPECT_TRUE(client->hello());
        const auto auth = client->registerUser(name, "mention-test-password");
        EXPECT_TRUE(auth);
        if (auth && result)
            *result = *auth;
        return client;
    }
    proto::Envelope call(RawClient& client, proto::Envelope request)
    {
        const auto reply = client.call(std::move(request));
        EXPECT_TRUE(reply);
        return reply.value_or(proto::Envelope{});
    }
    void SetUp() override
    {
        ASSERT_TRUE(server.start());
        owner = account("mention-owner");
        member = account("mention-member", &memberAuth);
        memberId = memberAuth.user().id();
        peer = account("mention-peer");
        proto::Envelope request;
        request.mutable_create_server()->set_name("Mention authority");
        auto reply = call(*owner, request);
        ASSERT_TRUE(reply.has_server());
        community = reply.server().id();
        request.Clear();
        request.mutable_create_invite()->set_server_id(community);
        reply = call(*owner, request);
        ASSERT_TRUE(reply.has_invite());
        request.Clear();
        request.mutable_join_invite()->set_token(reply.invite().token());
        ASSERT_TRUE(call(*member, request).has_server());
        ASSERT_TRUE(call(*peer, request).has_server());
        request.Clear();
        request.mutable_create_channel()->set_server_id(community);
        request.mutable_create_channel()->set_name("attention");
        reply = call(*owner, request);
        ASSERT_TRUE(reply.has_channel());
        channel = reply.channel().id();
        request.Clear();
        request.mutable_create_role()->set_server_id(community);
        request.mutable_create_role()->set_name("Reviewers");
        reply = call(*owner, request);
        ASSERT_TRUE(reply.has_role());
        roleId = reply.role().id();
        assign(true);
        owner->clearEvents();
        member->clearEvents();
        peer->clearEvents();
    }
    void assign(bool add)
    {
        proto::Envelope request;
        auto* assignment = request.mutable_assign_role();
        assignment->set_server_id(community);
        assignment->set_role_id(roleId);
        assignment->set_user_id(memberId);
        assignment->set_add(add);
        ASSERT_TRUE(call(*owner, request).has_ok());
    }
    proto::Envelope send(RawClient& client, const std::string& content, const std::string& operation = {})
    {
        proto::Envelope request;
        request.mutable_send_message()->set_channel_id(channel);
        request.mutable_send_message()->set_content(content);
        request.mutable_send_message()->set_operation_id(operation);
        return call(client, request);
    }
    std::string token() const { return "@{role:" + std::to_string(roleId) + "}"; }
    proto::Envelope attention(RawClient& client)
    {
        proto::Envelope request;
        request.mutable_get_attention_summary()->set_channel_id(channel);
        return call(client, request);
    }
    proto::ChatMessage history(RawClient& client, std::uint64_t id)
    {
        proto::Envelope request;
        request.mutable_get_messages()->set_channel_id(channel);
        const auto response = call(client, request);
        EXPECT_TRUE(response.has_message_page());
        for (const auto& message : response.message_page().messages())
            if (message.id() == id)
                return message;
        ADD_FAILURE() << "Missing message " << id;
        return {};
    }
};
} // namespace

TEST_F(MentionProtocol, OverlappingReasonsAreViewerCorrectInRepliesLiveHistorySearchAndSummary)
{
    const auto sent = send(*owner, "@mention-member " + token() + " @{everyone}");
    ASSERT_TRUE(sent.has_chat_message());
    const auto id = sent.chat_message().id();
    EXPECT_EQ(sent.chat_message().mention_ids_size(), 1);
    EXPECT_EQ(sent.chat_message().mention_classification().reason_mask(), 0u);
    const auto predicate
        = [id](const proto::Event& event) { return event.has_message_create() && event.message_create().id() == id; };
    const auto memberEvent = member->waitEvent(predicate);
    const auto peerEvent = peer->waitEvent(predicate);
    ASSERT_TRUE(memberEvent);
    ASSERT_TRUE(peerEvent);
    EXPECT_EQ(memberEvent->message_create().mention_classification().reason_mask(), 7u);
    EXPECT_EQ(peerEvent->message_create().mention_classification().reason_mask(), 4u);
    EXPECT_EQ(history(*member, id).mention_classification().reason_mask(), 7u);
    EXPECT_EQ(history(*peer, id).mention_classification().reason_mask(), 4u);
    proto::Envelope request;
    request.mutable_search_messages()->set_channel_id(channel);
    request.mutable_search_messages()->set_query("mention-member");
    const auto search = call(*member, request);
    ASSERT_TRUE(search.has_message_page());
    ASSERT_EQ(search.message_page().messages_size(), 1);
    EXPECT_EQ(search.message_page().messages(0).mention_classification().reason_mask(), 7u);
    const auto summary = attention(*member);
    ASSERT_TRUE(summary.has_attention_summary());
    EXPECT_EQ(summary.attention_summary().unread().mentions(), 1u);
    EXPECT_TRUE(summary.attention_summary().classification_complete());
    EXPECT_EQ(summary.attention_summary().mask_counts(6), 1u);
    const auto peerSummary = attention(*peer);
    ASSERT_TRUE(peerSummary.has_attention_summary());
    EXPECT_EQ(peerSummary.attention_summary().unread().mentions(), 0u);
    EXPECT_EQ(peerSummary.attention_summary().mask_counts(3), 1u);
}

TEST_F(MentionProtocol, DeniedBroadSendAndEditAreAtomicAndMembershipChangesDoNotRewriteSnapshots)
{
    auto denied = send(*member, token());
    ASSERT_TRUE(denied.has_error());
    EXPECT_EQ(denied.error().code(), proto::ERROR_PERMISSION_DENIED);
    const auto original = send(*member, "ordinary content");
    ASSERT_TRUE(original.has_chat_message());
    proto::Envelope request;
    request.mutable_edit_message()->set_message_id(original.chat_message().id());
    request.mutable_edit_message()->set_content("denied edit " + token());
    denied = call(*member, request);
    ASSERT_TRUE(denied.has_error());
    EXPECT_EQ(history(*owner, original.chat_message().id()).content(), "ordinary content");
    const auto broad = send(*owner, token());
    ASSERT_TRUE(broad.has_chat_message());
    assign(false);
    EXPECT_EQ(history(*member, broad.chat_message().id()).mention_classification().reason_mask(), 2u);
    request.Clear();
    request.mutable_edit_message()->set_message_id(broad.chat_message().id());
    request.mutable_edit_message()->set_content(token());
    ASSERT_TRUE(call(*owner, request).has_chat_message());
    EXPECT_EQ(history(*member, broad.chat_message().id()).mention_classification().reason_mask(), 0u);
    request.Clear();
    request.mutable_set_override()->mutable_override()->set_channel_id(channel);
    request.mutable_set_override()->mutable_override()->set_target_type(proto::PermissionOverride::TARGET_USER);
    request.mutable_set_override()->mutable_override()->set_target_id(memberId);
    request.mutable_set_override()->mutable_override()->set_deny(permissions::ViewChannel);
    ASSERT_TRUE(call(*owner, request).has_ok());
    const auto inaccessible = attention(*member);
    ASSERT_TRUE(inaccessible.has_error());
    EXPECT_EQ(inaccessible.error().code(), proto::ERROR_NOT_FOUND);
}

TEST_F(MentionProtocol, IdempotentReplayAndResumedEventsRetainOriginalRevision)
{
    const auto first = send(*owner, token(), std::string(16, 'x'));
    ASSERT_TRUE(first.has_chat_message());
    const auto id = first.chat_message().id();
    auto created = member->waitEvent(
        [id](const proto::Event& e) { return e.has_message_create() && e.message_create().id() == id; });
    ASSERT_TRUE(created);
    const auto sequence = created->sequence() - 1;
    member->abort();
    assign(false);
    const auto replay = send(*owner, token(), std::string(16, 'x'));
    ASSERT_TRUE(replay.has_chat_message());
    EXPECT_EQ(replay.chat_message().id(), id);
    proto::Envelope request;
    request.mutable_edit_message()->set_message_id(id);
    request.mutable_edit_message()->set_content("edited ordinary");
    ASSERT_TRUE(call(*owner, request).has_chat_message());
    member = std::make_unique<RawClient>(server);
    ASSERT_TRUE(member->connect());
    ASSERT_TRUE(member->hello());
    request.Clear();
    request.mutable_resume()->set_access_token(memberAuth.access_token());
    request.mutable_resume()->set_session_id(memberAuth.session_id());
    request.mutable_resume()->set_last_sequence(sequence);
    const auto resumed = call(*member, request);
    ASSERT_TRUE(resumed.has_resume_result());
    const auto old = member->waitEvent(
        [id](const proto::Event& e) { return e.has_message_create() && e.message_create().id() == id; });
    ASSERT_TRUE(old);
    EXPECT_EQ(old->message_create().mention_classification().reason_mask(), 2u);
    EXPECT_EQ(old->message_create().content(), token());
    const auto edit = member->waitEvent(
        [id](const proto::Event& e) { return e.has_message_update() && e.message_update().id() == id; });
    ASSERT_TRUE(edit);
    EXPECT_EQ(edit->message_update().mention_classification().reason_mask(), 0u);
    EXPECT_EQ(edit->message_update().content(), "edited ordinary");
}

TEST_F(MentionProtocol, CanonicalBroadcastsAreUnsupportedInPlaintextConversationsAndMalformedTokensStayLiteral)
{
    const auto malformed = send(*member, "@{role:01} @{role:-1} @{role:18446744073709551616} @everyone");
    ASSERT_TRUE(malformed.has_chat_message());
    EXPECT_EQ(malformed.chat_message().mention_classification().targets_size(), 0);
    proto::Envelope request;
    request.mutable_open_dm()->set_user_id(memberId);
    const auto opened = call(*owner, request);
    ASSERT_TRUE(opened.has_channel());
    channel = opened.channel().id();
    const auto dm = send(*owner, "@{everyone}");
    ASSERT_TRUE(dm.has_error());
    EXPECT_EQ(dm.error().code(), proto::ERROR_BAD_REQUEST);
}

TEST_F(MentionProtocol, IndependentChannelPermissionsAndForeignRoleTargets)
{
    proto::Envelope request;
    auto* override = request.mutable_set_override()->mutable_override();
    override->set_channel_id(channel);
    override->set_target_type(proto::PermissionOverride::TARGET_USER);
    override->set_target_id(memberId);
    override->set_allow(permissions::MentionRoles);
    ASSERT_TRUE(call(*owner, request).has_ok());
    EXPECT_TRUE(send(*member, token()).has_chat_message());
    const auto everyone = send(*member, "@{everyone}");
    ASSERT_TRUE(everyone.has_error());
    EXPECT_EQ(everyone.error().code(), proto::ERROR_PERMISSION_DENIED);
    request.Clear();
    request.mutable_create_server()->set_name("Foreign roles");
    const auto other = call(*owner, request);
    ASSERT_TRUE(other.has_server());
    request.Clear();
    request.mutable_create_role()->set_server_id(other.server().id());
    request.mutable_create_role()->set_name("Foreign secret");
    const auto foreign = call(*owner, request);
    ASSERT_TRUE(foreign.has_role());
    const auto bad = send(*owner, "@{role:" + std::to_string(foreign.role().id()) + "}");
    ASSERT_TRUE(bad.has_error());
    EXPECT_EQ(bad.error().code(), proto::ERROR_NOT_FOUND);
    EXPECT_EQ(bad.error().message().find("secret"), std::string::npos);
}

TEST_F(MentionProtocol, ExcessTargetsFailEntireSendAndOverlongIndividualDoesNotMatchPrefix)
{
    std::string tokens;
    // Reject the count before resolving targets; no foreign-role metadata is needed.
    for (int index = 0; index < 50; ++index)
        tokens += "@{role:" + std::to_string(1000 + index) + "} ";
    const auto excess = send(*owner, tokens + "@{everyone}");
    ASSERT_TRUE(excess.has_error());
    EXPECT_EQ(excess.error().code(), proto::ERROR_BAD_REQUEST);
    const auto empty = attention(*member);
    ASSERT_TRUE(empty.has_attention_summary());
    EXPECT_EQ(empty.attention_summary().unread().count(), 0u);
    auto prefix = account(std::string(32, 'a'));
    proto::Envelope request;
    request.mutable_create_invite()->set_server_id(community);
    auto invite = call(*owner, request);
    ASSERT_TRUE(invite.has_invite());
    request.Clear();
    request.mutable_join_invite()->set_token(invite.invite().token());
    ASSERT_TRUE(call(*prefix, request).has_server());
    const auto longToken = send(*owner, "@" + std::string(33, 'a'));
    ASSERT_TRUE(longToken.has_chat_message());
    EXPECT_EQ(longToken.chat_message().mention_ids_size(), 0);
    const auto exact = send(*owner, "@" + std::string(32, 'a'));
    ASSERT_TRUE(exact.has_chat_message());
    EXPECT_EQ(exact.chat_message().mention_ids_size(), 1);
}

TEST_F(MentionProtocol, AcceptedIdempotentReplayDoesNotReclassifyAfterLosingSendPermission)
{
    const std::string operation(16, 'r');
    const auto accepted = send(*member, "accepted original", operation);
    ASSERT_TRUE(accepted.has_chat_message());
    proto::Envelope request;
    auto* override = request.mutable_set_override()->mutable_override();
    override->set_channel_id(channel);
    override->set_target_type(proto::PermissionOverride::TARGET_USER);
    override->set_target_id(memberId);
    override->set_deny(permissions::SendMessages);
    ASSERT_TRUE(call(*owner, request).has_ok());
    const auto replay = send(*member, "accepted original", operation);
    ASSERT_TRUE(replay.has_chat_message());
    EXPECT_EQ(replay.chat_message().id(), accepted.chat_message().id());
    const auto fresh = send(*member, "new operation");
    ASSERT_TRUE(fresh.has_error());
    EXPECT_EQ(fresh.error().code(), proto::ERROR_PERMISSION_DENIED);
}
