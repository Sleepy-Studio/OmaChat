// Server-only acceptance: no daemon APIs are needed to deploy signaling.
#include "Harness.hpp"
#include <gtest/gtest.h>

using namespace omachat;
using namespace omachat::test;

namespace {
class DirectCallProtocol : public ::testing::Test {
protected:
    TestServer server;
    std::unique_ptr<RawClient> alice, bob;
    std::uint64_t dm = 0;
    proto::Envelope request(RawClient& client, proto::Envelope env)
    {
        auto reply = client.call(std::move(env));
        EXPECT_TRUE(reply);
        return reply.value_or(proto::Envelope{});
    }
    std::unique_ptr<RawClient> account(const char* name)
    {
        auto client = std::make_unique<RawClient>(server);
        EXPECT_TRUE(client->connect());
        EXPECT_TRUE(client->hello());
        EXPECT_TRUE(client->registerUser(name, "direct-call-password"));
        return client;
    }
    void SetUp() override
    {
        ASSERT_TRUE(server.start());
        alice = account("raw-call-alice");
        bob = account("raw-call-bob");
        proto::Envelope env;
        env.mutable_create_server()->set_name("Raw call lab");
        const auto community = request(*alice, env);
        ASSERT_TRUE(community.has_server());
        env.Clear();
        env.mutable_create_invite()->set_server_id(community.server().id());
        const auto invite = request(*alice, env);
        ASSERT_TRUE(invite.has_invite());
        env.Clear();
        env.mutable_join_invite()->set_token(invite.invite().token());
        ASSERT_FALSE(request(*bob, env).has_error());
        env.Clear();
        env.mutable_sync();
        const auto sync = request(*alice, env);
        ASSERT_TRUE(sync.has_sync_state());
        std::uint64_t other = 0;
        for (const auto& user : sync.sync_state().users())
            if (user.username() == "raw-call-bob")
                other = user.id();
        ASSERT_NE(other, 0u);
        env.Clear();
        env.mutable_open_dm()->set_user_id(other);
        const auto room = request(*alice, env);
        ASSERT_TRUE(room.has_channel());
        dm = room.channel().id();
    }
    proto::Envelope start()
    {
        proto::Envelope env;
        env.mutable_start_direct_call()->set_channel_id(dm);
        return request(*alice, env);
    }
    proto::Envelope act(RawClient& client, std::uint64_t id, const char* action)
    {
        proto::Envelope env;
        env.mutable_direct_call_action()->set_call_id(id);
        env.mutable_direct_call_action()->set_action(action);
        return request(client, env);
    }
    proto::Envelope join(RawClient& client, bool transfer = false)
    {
        proto::Envelope env;
        env.mutable_join_voice()->set_channel_id(dm);
        env.mutable_join_voice()->set_transfer(transfer);
        return request(client, env);
    }
};
} // namespace

TEST_F(DirectCallProtocol, AcceptanceSelectsOwnersBeforeAnyMediaLeaseAndHangupRevokesBoth)
{
    auto call = start();
    ASSERT_TRUE(call.has_direct_call());
    const auto id = call.direct_call().id();
    EXPECT_EQ(call.direct_call().state(), "ringing");
    EXPECT_TRUE(join(*alice).has_error());
    EXPECT_TRUE(join(*bob).has_error());
    RawClient second(server);
    ASSERT_TRUE(second.connect());
    ASSERT_TRUE(second.hello());
    ASSERT_TRUE(second.login("raw-call-bob", "direct-call-password"));
    ASSERT_FALSE(act(second, id, "accept").has_error());
    EXPECT_TRUE(act(*bob, id, "accept").has_error());
    EXPECT_TRUE(act(*bob, id, "hangup").has_error());
    EXPECT_TRUE(join(*bob, true).has_error());
    const auto callerMedia = join(*alice);
    const auto calleeMedia = join(second);
    ASSERT_TRUE(callerMedia.has_voice_session());
    ASSERT_TRUE(calleeMedia.has_voice_session());
    EXPECT_EQ(callerMedia.voice_session().channel_id(), dm);
    EXPECT_EQ(calleeMedia.voice_session().channel_id(), dm);
    EXPECT_EQ(callerMedia.voice_session().media_key().size(), 32u);
    EXPECT_NE(callerMedia.voice_session().stream_id(), calleeMedia.voice_session().stream_id());
    ASSERT_FALSE(act(*alice, id, "hangup").has_error());
    const auto ended = second.waitEvent([id](const proto::Event& event) {
        return event.has_direct_call_changed() && event.direct_call_changed().id() == id
            && event.direct_call_changed().state() == "ended";
    });
    ASSERT_TRUE(ended);
    EXPECT_EQ(ended->direct_call_changed().reason(), "hangup");
    EXPECT_TRUE(join(*alice).has_error());
    EXPECT_TRUE(join(second).has_error());
    EXPECT_TRUE(start().has_direct_call()); // reservations released for both accounts
}

TEST_F(DirectCallProtocol, CancelDeclineBusyAndNonownerLeaveAreAuthoritative)
{
    auto call = start();
    ASSERT_TRUE(call.has_direct_call());
    auto id = call.direct_call().id();
    EXPECT_TRUE(start().has_error());
    RawClient second(server);
    ASSERT_TRUE(second.connect());
    ASSERT_TRUE(second.hello());
    ASSERT_TRUE(second.login("raw-call-alice", "direct-call-password"));
    EXPECT_TRUE(act(second, id, "cancel").has_error());
    proto::Envelope leave;
    leave.mutable_leave_voice();
    EXPECT_TRUE(request(second, leave).has_error());
    EXPECT_TRUE(act(*bob, id, "cancel").has_error());
    ASSERT_FALSE(act(*bob, id, "decline").has_error());
    call = start();
    ASSERT_TRUE(call.has_direct_call());
    id = call.direct_call().id();
    ASSERT_FALSE(act(*alice, id, "cancel").has_error());
    EXPECT_TRUE(act(*bob, id, "accept").has_error());
    EXPECT_TRUE(join(*alice).has_error());
}

TEST_F(DirectCallProtocol, OwnerDisconnectAndOfflineRecipientReleaseReservation)
{
    const auto call = start();
    ASSERT_TRUE(call.has_direct_call());
    alice->abort();
    const auto ended = bob->waitEvent([](const proto::Event& event) {
        return event.has_direct_call_changed() && event.direct_call_changed().reason() == "disconnected";
    });
    ASSERT_TRUE(ended);
    EXPECT_EQ(ended->direct_call_changed().state(), "ended");
    EXPECT_TRUE(join(*bob).has_error());
    RawClient reconnect(server);
    ASSERT_TRUE(reconnect.connect());
    ASSERT_TRUE(reconnect.hello());
    ASSERT_TRUE(reconnect.login("raw-call-alice", "direct-call-password"));
    bob->abort();
    waitFor([] { return false; }, 50);
    proto::Envelope env;
    env.mutable_start_direct_call()->set_channel_id(dm);
    const auto offline = request(reconnect, env);
    ASSERT_TRUE(offline.has_error());
    EXPECT_EQ(offline.error().message(), "offline");
}

TEST_F(DirectCallProtocol, RingTimeoutInvalidatesAcceptanceAndMedia)
{
    const auto call = start();
    ASSERT_TRUE(call.has_direct_call());
    const auto id = call.direct_call().id();
    const auto ended = bob->waitEvent(
        [id](const proto::Event& event) {
            return event.has_direct_call_changed() && event.direct_call_changed().id() == id
                && event.direct_call_changed().reason() == "timeout";
        },
        32000);
    ASSERT_TRUE(ended);
    EXPECT_EQ(ended->direct_call_changed().state(), "ended");
    EXPECT_TRUE(act(*bob, id, "accept").has_error());
    EXPECT_TRUE(join(*alice).has_error());
    EXPECT_TRUE(join(*bob).has_error());
    EXPECT_TRUE(start().has_direct_call());
}
