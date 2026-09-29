// Server behaviour over the real TLS protocol: authentication, sessions,
// real-time events, history, permissions and abuse limits.

#include "Harness.hpp"
#include "omachat/core/Permissions.hpp"

#include <QCryptographicHash>

#include <gtest/gtest.h>

using namespace omachat;
using namespace omachat::test;

namespace {

struct Fixture : ::testing::Test {
    TestServer server;
    void SetUp() override { ASSERT_TRUE(server.start()); }

    std::unique_ptr<RawClient> client(const std::string& user, const std::string& password = "correct horse",
        std::optional<proto::AuthResult>* auth = nullptr)
    {
        auto c = std::make_unique<RawClient>(server);
        EXPECT_TRUE(c->connect());
        EXPECT_TRUE(c->hello());
        auto r = c->registerUser(user, password);
        if (!r)
            r = c->login(user, password);
        EXPECT_TRUE(r.has_value()) << user;
        if (auth)
            *auth = r;
        return c;
    }

    proto::SyncState sync(RawClient& c)
    {
        proto::Envelope env;
        env.mutable_sync();
        auto r = c.call(env);
        EXPECT_TRUE(r && r->has_sync_state());
        return r ? r->sync_state() : proto::SyncState{};
    }

    std::uint64_t createServer(RawClient& c, const std::string& name)
    {
        proto::Envelope env;
        env.mutable_create_server()->set_name(name);
        auto r = c.call(env);
        EXPECT_TRUE(r && r->has_server());
        return r ? r->server().id() : 0;
    }

    std::string invite(RawClient& c, std::uint64_t serverId, std::uint32_t maxUses = 0)
    {
        proto::Envelope env;
        env.mutable_create_invite()->set_server_id(serverId);
        env.mutable_create_invite()->set_max_uses(maxUses);
        auto r = c.call(env);
        EXPECT_TRUE(r && r->has_invite());
        return r ? r->invite().token() : std::string();
    }

    proto::Envelope join(RawClient& c, const std::string& token)
    {
        proto::Envelope env;
        env.mutable_join_invite()->set_token(token);
        return c.call(env).value_or(proto::Envelope{});
    }

    std::uint64_t channelNamed(const proto::SyncState& s, const std::string& name, proto::ChannelType type)
    {
        for (const auto& c : s.channels())
            if (c.name() == name && c.type() == type)
                return c.id();
        return 0;
    }

    proto::Envelope send(RawClient& c, std::uint64_t channel, const std::string& text)
    {
        proto::Envelope env;
        env.mutable_send_message()->set_channel_id(channel);
        env.mutable_send_message()->set_content(text);
        return c.call(env).value_or(proto::Envelope{});
    }
};

} // namespace

TEST_F(Fixture, DeletingAccountRemovesServerIdentityAndOwnedServers)
{
    auto alice = client("alice");
    auto bob = client("bob");
    const auto sid = createServer(*alice, "Temporary");
    ASSERT_TRUE(join(*bob, invite(*alice, sid)).has_server());

    proto::Envelope request;
    request.mutable_delete_account();
    auto deleted = alice->call(request);
    ASSERT_TRUE(deleted && deleted->has_ok());
    EXPECT_TRUE(bob->waitEvent([sid](const proto::Event& e) {
        return e.has_server_delete() && e.server_delete().server_id() == sid;
    }));

    auto retry = std::make_unique<RawClient>(server);
    ASSERT_TRUE(retry->connect());
    ASSERT_TRUE(retry->hello());
    EXPECT_FALSE(retry->login("alice", "correct horse"));
    EXPECT_TRUE(retry->registerUser("alice", "correct horse")) << "deleted username can be registered again";
}

TEST_F(Fixture, DeletingMemberRemovesMembershipAndPrivateConversation)
{
    auto alice = client("alice");
    auto bob = client("bob");
    const auto sid = createServer(*alice, "Keep This");
    ASSERT_TRUE(join(*bob, invite(*alice, sid)).has_server());
    const auto aliceId = sync(*alice).self().id();
    const auto bobId = sync(*bob).self().id();
    proto::Envelope open;
    open.mutable_open_dm()->set_user_id(bobId);
    const auto dm = alice->call(open);
    ASSERT_TRUE(dm && dm->has_channel());
    const auto dmId = dm->channel().id();

    proto::Envelope request;
    request.mutable_delete_account();
    ASSERT_TRUE(bob->call(request)->has_ok());
    EXPECT_TRUE(alice->waitEvent([sid, bobId](const proto::Event& e) {
        return e.has_member_leave() && e.member_leave().server_id() == sid && e.member_leave().user_id() == bobId;
    }));
    EXPECT_TRUE(alice->waitEvent([dmId](const proto::Event& e) {
        return e.has_channel_delete() && e.channel_delete().channel_id() == dmId;
    }));
    const auto state = sync(*alice);
    ASSERT_EQ(state.servers_size(), 1);
    EXPECT_EQ(state.members_size(), 1);
    EXPECT_EQ(state.members(0).user_id(), aliceId);
}

TEST_F(Fixture, ProfileUpdatesAreValidatedAndVisibleToOtherMembers)
{
    auto alice = client("alice");
    auto bob = client("bob");
    const auto sid = createServer(*alice, "Friends");
    ASSERT_TRUE(join(*bob, invite(*alice, sid)).has_server());

    proto::Envelope invalid;
    auto* bad = invalid.mutable_update_profile();
    bad->set_display_name("Alice");
    bad->set_avatar_url("http://example.org/avatar.png");
    bad->set_bio("hello");
    auto rejected = alice->call(invalid);
    ASSERT_TRUE(rejected && rejected->has_error());
    EXPECT_EQ(rejected->error().code(), proto::ERROR_BAD_REQUEST);

    proto::Envelope update;
    auto* profile = update.mutable_update_profile();
    profile->set_display_name("Alice Cooper");
    profile->set_avatar_url("https://example.org/avatar.png");
    profile->set_bio("Hello there");
    auto result = alice->call(update);
    ASSERT_TRUE(result && result->has_ok());
    auto event = bob->waitEvent([](const proto::Event& e) {
        return e.has_user_update() && e.user_update().username() == "alice";
    });
    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(event->user_update().display_name(), "Alice Cooper");
    EXPECT_EQ(event->user_update().bio(), "Hello there");
    EXPECT_EQ(event->user_update().avatar_url(), "https://example.org/avatar.png");

    const auto state = sync(*bob);
    bool found = false;
    for (const auto& user : state.users()) {
        if (user.username() == "alice") {
            found = true;
            EXPECT_EQ(user.bio(), "Hello there");
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(Fixture, ProtocolMajorMismatchIsRejected)
{
    RawClient c(server);
    ASSERT_TRUE(c.connect());
    proto::Envelope env;
    env.mutable_hello()->set_protocol_major(99);
    auto r = c.call(env);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->error().code(), proto::ERROR_PROTOCOL_MISMATCH);
}

TEST_F(Fixture, RequestsBeforeAuthenticationAreRefused)
{
    RawClient c(server);
    ASSERT_TRUE(c.connect());
    ASSERT_TRUE(c.hello());
    proto::Envelope env;
    env.mutable_sync();
    auto r = c.call(env);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->error().code(), proto::ERROR_NOT_AUTHENTICATED);
}

TEST_F(Fixture, RegisterLoginAndInvalidLogin)
{
    std::optional<proto::AuthResult> auth;
    auto alice = client("alice", "correct horse", &auth);
    ASSERT_TRUE(auth);
    EXPECT_FALSE(auth->refresh_token().empty());
    EXPECT_FALSE(auth->access_token().empty());

    RawClient dup(server);
    ASSERT_TRUE(dup.connect() && dup.hello());
    proto::Envelope reg;
    reg.mutable_register_()->set_username("ALICE");
    reg.mutable_register_()->set_password("another password");
    auto r = dup.call(reg, 20000);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->error().code(), proto::ERROR_CONFLICT) << "usernames are case-insensitive";

    RawClient bad(server);
    ASSERT_TRUE(bad.connect() && bad.hello());
    proto::Envelope login;
    login.mutable_login()->set_username("alice");
    login.mutable_login()->set_password("wrong password");
    auto lr = bad.call(login, 20000);
    ASSERT_TRUE(lr);
    EXPECT_EQ(lr->error().code(), proto::ERROR_AUTHENTICATION);

    RawClient good(server);
    ASSERT_TRUE(good.connect() && good.hello());
    EXPECT_TRUE(good.login("alice", "correct horse"));
}

TEST_F(Fixture, RefreshTokenRotatesAndOldTokenDies)
{
    std::optional<proto::AuthResult> auth;
    auto alice = client("carol", "correct horse", &auth);
    const std::string refresh = auth->refresh_token();

    RawClient second(server);
    ASSERT_TRUE(second.connect() && second.hello());
    proto::Envelope env;
    env.mutable_refresh()->set_refresh_token(refresh);
    auto r = second.call(env);
    ASSERT_TRUE(r && r->has_auth_result());
    EXPECT_NE(r->auth_result().refresh_token(), refresh);

    RawClient third(server);
    ASSERT_TRUE(third.connect() && third.hello());
    auto again = third.call(env);
    ASSERT_TRUE(again);
    EXPECT_EQ(again->error().code(), proto::ERROR_AUTHENTICATION) << "refresh tokens are single use";
}

TEST_F(Fixture, SessionResumeReplaysMissedEvents)
{
    std::optional<proto::AuthResult> aliceAuth;
    auto alice = client("alice", "correct horse", &aliceAuth);
    auto bob = client("bob");
    const auto sid = createServer(*alice, "Resume Test");
    ASSERT_EQ(join(*bob, invite(*alice, sid)).has_server(), true);
    const auto state = sync(*alice);
    const auto general = channelNamed(state, "general", proto::CHANNEL_TYPE_TEXT);

    // Alice drops off; Bob talks while she is away.
    alice->abort();
    ASSERT_TRUE(send(*bob, general, "you missed this").has_chat_message());

    RawClient back(server);
    ASSERT_TRUE(back.connect() && back.hello());
    proto::Envelope resume;
    resume.mutable_resume()->set_access_token(aliceAuth->access_token());
    resume.mutable_resume()->set_session_id(aliceAuth->session_id());
    resume.mutable_resume()->set_last_sequence(state.last_sequence());
    auto r = back.call(resume);
    ASSERT_TRUE(r && r->has_resume_result()) << (r ? r->error().message() : "no reply");
    EXPECT_GE(r->resume_result().replayed_events(), 1u);
    auto missed = back.waitEvent([](const proto::Event& e) { return e.has_message_create(); });
    ASSERT_TRUE(missed);
    EXPECT_EQ(missed->message_create().content(), "you missed this");
}

TEST_F(Fixture, ResumeFromUnknownSequenceRequiresFullSync)
{
    std::optional<proto::AuthResult> auth;
    auto alice = client("dave", "correct horse", &auth);
    RawClient back(server);
    ASSERT_TRUE(back.connect() && back.hello());
    proto::Envelope resume;
    resume.mutable_resume()->set_access_token(auth->access_token());
    resume.mutable_resume()->set_session_id(auth->session_id());
    resume.mutable_resume()->set_last_sequence(1'000'000); // from the future
    auto r = back.call(resume);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->error().code(), proto::ERROR_RESUME_FAILED);
    // ...but the connection is authenticated and can sync.
    EXPECT_TRUE(sync(back).has_self());
}

TEST_F(Fixture, TwoClientsExchangeMessagesInRealTime)
{
    auto alice = client("alice");
    auto bob = client("bob");
    const auto sid = createServer(*alice, "Sleepy Studio");
    ASSERT_TRUE(join(*bob, invite(*alice, sid)).has_server());
    const auto general = channelNamed(sync(*alice), "general", proto::CHANNEL_TYPE_TEXT);
    ASSERT_NE(general, 0u);

    auto sent = send(*alice, general, "hello @bob, `code` here");
    ASSERT_TRUE(sent.has_chat_message());
    auto ev = bob->waitEvent([](const proto::Event& e) { return e.has_message_create(); });
    ASSERT_TRUE(ev);
    EXPECT_EQ(ev->message_create().content(), "hello @bob, `code` here");
    ASSERT_EQ(ev->message_create().mention_ids_size(), 1);

    // Edit and delete propagate too.
    proto::Envelope edit;
    edit.mutable_edit_message()->set_message_id(sent.chat_message().id());
    edit.mutable_edit_message()->set_content("edited");
    ASSERT_TRUE(alice->call(edit)->has_chat_message());
    auto upd = bob->waitEvent([](const proto::Event& e) { return e.has_message_update(); });
    ASSERT_TRUE(upd);
    EXPECT_GT(upd->message_update().edited_at(), 0);

    proto::Envelope del;
    del.mutable_delete_message()->set_message_id(sent.chat_message().id());
    // Bob cannot delete Alice's message...
    auto denied = bob->call(del);
    EXPECT_EQ(denied->error().code(), proto::ERROR_PERMISSION_DENIED);
    // ...Alice can.
    EXPECT_TRUE(alice->call(del)->has_ok());
    EXPECT_TRUE(bob->waitEvent([](const proto::Event& e) { return e.has_message_delete(); }));
}

TEST_F(Fixture, HistoryIsPaginated)
{
    auto alice = client("alice");
    const auto sid = createServer(*alice, "History");
    const auto general = channelNamed(sync(*alice), "general", proto::CHANNEL_TYPE_TEXT);
    (void)sid;
    for (int i = 0; i < 60; ++i) {
        auto r = send(*alice, general, "msg " + std::to_string(i));
        if (r.has_error() && r.error().code() == proto::ERROR_RATE_LIMITED) {
            waitFor([] { return false; }, r.error().retry_after_ms() + 10);
            --i;
            continue;
        }
        ASSERT_TRUE(r.has_chat_message());
    }
    proto::Envelope get;
    get.mutable_get_messages()->set_channel_id(general);
    auto page = alice->call(get);
    ASSERT_TRUE(page && page->has_message_page());
    EXPECT_EQ(page->message_page().messages_size(), 50) << "default page is 50";
    EXPECT_TRUE(page->message_page().has_more());
    EXPECT_EQ(page->message_page().messages(0).content(), "msg 59");
    get.mutable_get_messages()->set_before_message_id(page->message_page().messages(49).id());
    auto older = alice->call(get);
    EXPECT_EQ(older->message_page().messages_size(), 10);
    EXPECT_FALSE(older->message_page().has_more());
}

TEST_F(Fixture, DiscordImportPreservesHistoryAndRequiresOwner)
{
    auto alice = client("alice");
    auto bob = client("bob");
    const auto serverId = createServer(*alice, "Imported place");
    ASSERT_TRUE(join(*bob, invite(*alice, serverId)).has_server());
    proto::Envelope request;
    auto* batch = request.mutable_import_discord_batch();
    batch->set_server_id(serverId);
    batch->set_guild_id("999");
    batch->set_channel_id("888");
    batch->set_channel_name("old-chat");
    batch->set_category_id("777");
    batch->set_category_name("Archive");
    auto* first = batch->add_messages();
    first->set_discord_id("100");
    first->set_author_id("111");
    first->set_author_name("Old Alice");
    first->set_timestamp(1577836800000); // 2020
    first->set_content("first from Discord");
    auto* asset = first->add_assets();
    asset->set_filename("photo.png");
    asset->set_mime_type("image/png");
    asset->set_data("image bytes");
    auto* second = batch->add_messages();
    second->set_discord_id("101");
    second->set_author_id("111");
    second->set_author_name("Old Alice");
    second->set_timestamp(1577836801000);
    second->set_content("reply from Discord");
    second->set_reply_discord_id("100");
    auto* mention = second->add_mentions();
    mention->set_id("222");
    mention->set_name("Mentioned user");
    auto* reaction = second->add_reactions();
    reaction->set_emoji("👍");
    auto* reactor = reaction->add_users();
    reactor->set_id("222");
    reactor->set_name("Mentioned user");

    auto denied = bob->call(request);
    ASSERT_TRUE(denied && denied->has_error());
    EXPECT_EQ(denied->error().code(), proto::ERROR_PERMISSION_DENIED);
    auto imported = alice->call(request);
    ASSERT_TRUE(imported && imported->has_import_discord_result()) << (imported ? imported->DebugString() : "no reply");
    const auto channelId = imported->import_discord_result().channel_id();
    EXPECT_EQ(imported->import_discord_result().imported(), 2u);
    auto again = alice->call(request);
    ASSERT_TRUE(again && again->has_import_discord_result());
    EXPECT_EQ(again->import_discord_result().imported(), 0u);

    const auto state = sync(*bob);
    EXPECT_EQ(channelNamed(state, "old-chat", proto::CHANNEL_TYPE_TEXT), channelId);
    proto::Envelope history;
    history.mutable_get_messages()->set_channel_id(channelId);
    auto page = bob->call(history);
    ASSERT_TRUE(page && page->has_message_page());
    ASSERT_EQ(page->message_page().messages_size(), 2);
    const auto& newest = page->message_page().messages(0);
    const auto& oldest = page->message_page().messages(1);
    EXPECT_EQ(newest.timestamp(), 1577836801000);
    EXPECT_EQ(newest.reply_to(), oldest.id());
    EXPECT_EQ(newest.mention_ids_size(), 1);
    ASSERT_EQ(newest.reactions_size(), 1);
    EXPECT_EQ(newest.reactions(0).count(), 1u);
    EXPECT_EQ(oldest.timestamp(), 1577836800000);
    ASSERT_EQ(oldest.attachments_size(), 1);
    EXPECT_EQ(oldest.attachments(0).filename(), "photo.png");
}

TEST_F(Fixture, PermissionsAreEnforcedServerSide)
{
    auto alice = client("alice");
    auto bob = client("bob");
    const auto sid = createServer(*alice, "Perms");
    ASSERT_TRUE(join(*bob, invite(*alice, sid)).has_server());
    auto state = sync(*alice);
    const auto general = channelNamed(state, "general", proto::CHANNEL_TYPE_TEXT);
    std::uint64_t memberRole = 0, guestRole = 0;
    for (const auto& r : state.roles()) {
        if (r.name() == "Member")
            memberRole = r.id();
        if (r.is_default())
            guestRole = r.id();
    }
    std::uint64_t bobId = 0;
    for (const auto& u : state.users())
        if (u.username() == "bob")
            bobId = u.id();
    ASSERT_TRUE(memberRole && guestRole && bobId);

    // Members may not administer.
    proto::Envelope del;
    del.mutable_delete_server()->set_server_id(sid);
    EXPECT_EQ(bob->call(del)->error().code(), proto::ERROR_PERMISSION_DENIED);
    proto::Envelope kick;
    kick.mutable_kick()->set_server_id(sid);
    kick.mutable_kick()->set_user_id(bobId);
    EXPECT_EQ(bob->call(kick)->error().code(), proto::ERROR_PERMISSION_DENIED);
    proto::Envelope mkchan;
    mkchan.mutable_create_channel()->set_server_id(sid);
    mkchan.mutable_create_channel()->set_name("sneaky");
    EXPECT_EQ(bob->call(mkchan)->error().code(), proto::ERROR_PERMISSION_DENIED);

    // Demote Bob to guest: he can read but no longer write.
    proto::Envelope demote;
    demote.mutable_assign_role()->set_server_id(sid);
    demote.mutable_assign_role()->set_user_id(bobId);
    demote.mutable_assign_role()->set_role_id(memberRole);
    demote.mutable_assign_role()->set_add(false);
    ASSERT_TRUE(alice->call(demote)->has_ok());
    EXPECT_TRUE(bob->waitEvent([](const proto::Event& e) { return e.has_permissions_changed(); }));
    EXPECT_EQ(send(*bob, general, "hi").error().code(), proto::ERROR_PERMISSION_DENIED);

    // A channel override grants Bob writing in #general only.
    proto::Envelope ov;
    auto* o = ov.mutable_set_override()->mutable_override();
    o->set_channel_id(general);
    o->set_target_type(proto::PermissionOverride::TARGET_USER);
    o->set_target_id(bobId);
    o->set_allow(permissions::SendMessages);
    ASSERT_TRUE(alice->call(ov)->has_ok());
    EXPECT_TRUE(send(*bob, general, "override works").has_chat_message());

    // Guests cannot grant themselves anything.
    proto::Envelope escalate;
    escalate.mutable_create_role()->set_server_id(sid);
    escalate.mutable_create_role()->set_name("root");
    escalate.mutable_create_role()->set_permissions(permissions::Administrator);
    EXPECT_EQ(bob->call(escalate)->error().code(), proto::ERROR_PERMISSION_DENIED);

    // Kick removes him; the kicked client is told.
    ASSERT_TRUE(alice->call(kick)->has_ok());
    auto left = bob->waitEvent([](const proto::Event& e) { return e.has_member_leave(); });
    ASSERT_TRUE(left);
    EXPECT_EQ(left->member_leave().reason(), "kicked");
    EXPECT_EQ(send(*bob, general, "still here?").error().code(), proto::ERROR_NOT_FOUND);
}

TEST_F(Fixture, BansAndInviteLimits)
{
    auto alice = client("alice");
    auto bob = client("bob");
    auto carol = client("carol");
    const auto sid = createServer(*alice, "Limits");
    const auto token = invite(*alice, sid, 1);
    EXPECT_TRUE(join(*bob, token).has_server());
    EXPECT_EQ(join(*carol, token).error().code(), proto::ERROR_INVITE_INVALID) << "single-use invite";
    EXPECT_EQ(join(*carol, "no-such-token").error().code(), proto::ERROR_INVITE_INVALID);

    std::uint64_t bobId = 0;
    const auto snapshot = sync(*alice);
    for (const auto& u : snapshot.users())
        if (u.username() == "bob")
            bobId = u.id();
    proto::Envelope ban;
    ban.mutable_ban()->set_server_id(sid);
    ban.mutable_ban()->set_user_id(bobId);
    ASSERT_TRUE(alice->call(ban)->has_ok());
    EXPECT_EQ(join(*bob, invite(*alice, sid)).error().code(), proto::ERROR_BANNED);
}

TEST_F(Fixture, VoiceJoinNegotiatesMediaSessionAndBroadcastsState)
{
    auto alice = client("alice");
    auto bob = client("bob");
    const auto sid = createServer(*alice, "Voice");
    ASSERT_TRUE(join(*bob, invite(*alice, sid)).has_server());
    const auto lounge = channelNamed(sync(*alice), "General", proto::CHANNEL_TYPE_VOICE);
    bob->clearEvents();

    proto::Envelope jv;
    jv.mutable_join_voice()->set_channel_id(lounge);
    auto r = alice->call(jv);
    ASSERT_TRUE(r && r->has_voice_session());
    EXPECT_EQ(r->voice_session().media_key().size(), 32u);
    EXPECT_EQ(r->voice_session().udp_port(), server.mediaPort());
    auto ev = bob->waitEvent([](const proto::Event& e) { return e.has_voice_state_update(); });
    ASSERT_TRUE(ev);
    EXPECT_EQ(ev->voice_state_update().channel_id(), lounge);

    // Text channels cannot be joined for voice.
    proto::Envelope bad;
    bad.mutable_join_voice()->set_channel_id(channelNamed(sync(*alice), "general", proto::CHANNEL_TYPE_TEXT));
    EXPECT_EQ(alice->call(bad)->error().code(), proto::ERROR_BAD_REQUEST);
}

TEST_F(Fixture, MessageFloodIsRateLimited)
{
    auto alice = client("alice");
    createServer(*alice, "Flood");
    const auto general = channelNamed(sync(*alice), "general", proto::CHANNEL_TYPE_TEXT);
    int limited = 0;
    for (int i = 0; i < 30; ++i) {
        auto r = send(*alice, general, "spam");
        if (r.has_error() && r.error().code() == proto::ERROR_RATE_LIMITED) {
            EXPECT_GT(r.error().retry_after_ms(), 0u);
            ++limited;
        }
    }
    EXPECT_GT(limited, 0);
}

TEST_F(Fixture, DirectMessagesRequireSharedServer)
{
    auto alice = client("alice");
    auto bob = client("bob");
    auto stranger = client("mallory");
    const auto sid = createServer(*alice, "DM");
    ASSERT_TRUE(join(*bob, invite(*alice, sid)).has_server());
    std::uint64_t bobId = 0, aliceId = 0;
    const auto snapshot = sync(*alice);
    for (const auto& u : snapshot.users()) {
        if (u.username() == "bob")
            bobId = u.id();
        if (u.username() == "alice")
            aliceId = u.id();
    }
    proto::Envelope open;
    open.mutable_open_dm()->set_user_id(bobId);
    auto dm = alice->call(open);
    ASSERT_TRUE(dm && dm->has_channel());
    EXPECT_EQ(dm->channel().type(), proto::CHANNEL_TYPE_DM);
    ASSERT_TRUE(send(*alice, dm->channel().id(), "psst").has_chat_message());
    EXPECT_TRUE(bob->waitEvent([](const proto::Event& e) { return e.has_message_create(); }));

    proto::Envelope spam;
    spam.mutable_open_dm()->set_user_id(aliceId);
    EXPECT_EQ(stranger->call(spam)->error().code(), proto::ERROR_PERMISSION_DENIED);
    EXPECT_EQ(send(*stranger, dm->channel().id(), "let me in").error().code(), proto::ERROR_NOT_FOUND);
}

namespace {

proto::Envelope beginUpload(RawClient& c, std::uint64_t channel, const std::string& name, std::uint64_t size)
{
    proto::Envelope env;
    auto* b = env.mutable_begin_upload();
    b->set_channel_id(channel);
    b->set_filename(name);
    b->set_mime_type("text/plain; charset=utf-8");
    b->set_size(size);
    return c.call(env).value_or(proto::Envelope{});
}

proto::Envelope uploadChunk(RawClient& c, std::uint64_t id, std::uint64_t offset, const std::string& data)
{
    proto::Envelope env;
    auto* ch = env.mutable_upload_chunk();
    ch->set_attachment_id(id);
    ch->set_offset(offset);
    ch->set_data(data);
    return c.call(env).value_or(proto::Envelope{});
}

proto::Envelope finishUpload(RawClient& c, std::uint64_t id, const std::string& sha256 = {})
{
    proto::Envelope env;
    env.mutable_finish_upload()->set_attachment_id(id);
    env.mutable_finish_upload()->set_sha256(sha256);
    return c.call(env).value_or(proto::Envelope{});
}

proto::Envelope download(RawClient& c, std::uint64_t id, std::uint64_t offset, std::uint32_t length = 0)
{
    proto::Envelope env;
    env.mutable_download()->set_attachment_id(id);
    env.mutable_download()->set_offset(offset);
    env.mutable_download()->set_length(length);
    return c.call(env).value_or(proto::Envelope{});
}

std::string sha256(const std::string& data)
{
    return QCryptographicHash::hash(
        QByteArrayView(data.data(), static_cast<qsizetype>(data.size())), QCryptographicHash::Sha256)
        .toStdString();
}

} // namespace

TEST_F(Fixture, AttachmentsUploadSendAndDownload)
{
    auto alice = client("alice");
    auto bob = client("bob");
    const auto sid = createServer(*alice, "Files");
    ASSERT_TRUE(join(*bob, invite(*alice, sid)).has_server());
    const auto general = channelNamed(sync(*alice), "general", proto::CHANNEL_TYPE_TEXT);

    // Larger than one chunk so the multi-chunk path is exercised.
    std::string data;
    for (int i = 0; data.size() < 700 * 1024; ++i)
        data += "line " + std::to_string(i) + "\n";

    const auto ticket = beginUpload(*alice, general, "../../etc/notes.txt", data.size());
    ASSERT_TRUE(ticket.has_upload_ticket()) << ticket.error().message();
    const auto id = ticket.upload_ticket().attachment_id();
    const std::size_t chunk = ticket.upload_ticket().chunk_size();
    ASSERT_GT(chunk, 0u);
    for (std::size_t off = 0; off < data.size(); off += chunk)
        ASSERT_TRUE(uploadChunk(*alice, id, off, data.substr(off, chunk)).has_ok());
    const auto done = finishUpload(*alice, id, sha256(data));
    ASSERT_TRUE(done.has_attachment()) << done.error().message();
    EXPECT_EQ(done.attachment().filename(), ".._.._etc_notes.txt"); // never a path
    EXPECT_EQ(done.attachment().mime_type(), "text/plain");
    EXPECT_EQ(done.attachment().size(), data.size());

    // Pending attachments are private to the uploader.
    EXPECT_EQ(download(*bob, id, 0).error().code(), proto::ERROR_NOT_FOUND);

    // Bob cannot send Alice's attachment; Alice can, with no text at all.
    proto::Envelope steal;
    steal.mutable_send_message()->set_channel_id(general);
    steal.mutable_send_message()->set_content("mine now");
    steal.mutable_send_message()->add_attachment_ids(id);
    EXPECT_EQ(bob->call(steal)->error().code(), proto::ERROR_BAD_REQUEST);

    proto::Envelope msg;
    msg.mutable_send_message()->set_channel_id(general);
    msg.mutable_send_message()->add_attachment_ids(id);
    auto sent = alice->call(msg);
    ASSERT_TRUE(sent && sent->has_chat_message()) << sent->error().message();
    ASSERT_EQ(sent->chat_message().attachments_size(), 1);

    auto created = bob->waitEvent([](const proto::Event& e) { return e.has_message_create(); });
    ASSERT_TRUE(created);
    ASSERT_EQ(created->message_create().attachments_size(), 1);
    EXPECT_EQ(created->message_create().attachments(0).id(), id);

    // An attachment can only be claimed once.
    EXPECT_EQ(alice->call(msg)->error().code(), proto::ERROR_BAD_REQUEST);

    // Bob downloads it back byte for byte.
    std::string got;
    while (got.size() < data.size()) {
        const auto c = download(*bob, id, got.size());
        ASSERT_TRUE(c.has_file_chunk()) << c.error().message();
        ASSERT_EQ(c.file_chunk().offset(), got.size());
        ASSERT_FALSE(c.file_chunk().data().empty());
        got += c.file_chunk().data();
    }
    EXPECT_EQ(got, data);

    // History carries attachment metadata.
    proto::Envelope hist;
    hist.mutable_get_messages()->set_channel_id(general);
    auto page = bob->call(hist);
    ASSERT_TRUE(page && page->message_page().messages_size() == 1);
    EXPECT_EQ(page->message_page().messages(0).attachments(0).filename(), ".._.._etc_notes.txt");

    // Deleting the message deletes the file.
    proto::Envelope del;
    del.mutable_delete_message()->set_message_id(sent->chat_message().id());
    ASSERT_TRUE(alice->call(del)->has_ok());
    EXPECT_EQ(download(*bob, id, 0).error().code(), proto::ERROR_NOT_FOUND);
}

TEST_F(Fixture, AttachmentUploadsAreValidated)
{
    auto alice = client("alice");
    auto bob = client("bob");
    const auto sid = createServer(*alice, "Limits");
    ASSERT_TRUE(join(*bob, invite(*alice, sid)).has_server());
    auto state = sync(*alice);
    const auto general = channelNamed(state, "general", proto::CHANNEL_TYPE_TEXT);
    const auto voice = channelNamed(state, "General", proto::CHANNEL_TYPE_VOICE);

    EXPECT_EQ(beginUpload(*alice, general, "big.bin", 51ull * 1024 * 1024).error().code(), proto::ERROR_TOO_LARGE);
    EXPECT_EQ(beginUpload(*alice, general, "empty", 0).error().code(), proto::ERROR_BAD_REQUEST);
    EXPECT_EQ(beginUpload(*alice, general, "..", 4).error().code(), proto::ERROR_BAD_REQUEST);
    if (voice) {
        EXPECT_EQ(beginUpload(*alice, voice, "a.txt", 4).error().code(), proto::ERROR_BAD_REQUEST);
    }

    // Out-of-order chunks, overruns and bad checksums cancel the upload.
    auto t = beginUpload(*alice, general, "a.txt", 4);
    ASSERT_TRUE(t.has_upload_ticket());
    EXPECT_EQ(uploadChunk(*alice, t.upload_ticket().attachment_id(), 2, "ab").error().code(), proto::ERROR_BAD_REQUEST);
    EXPECT_EQ(uploadChunk(*alice, t.upload_ticket().attachment_id(), 0, "abcd").error().code(), proto::ERROR_NOT_FOUND);

    t = beginUpload(*alice, general, "b.txt", 4);
    ASSERT_TRUE(uploadChunk(*alice, t.upload_ticket().attachment_id(), 0, "abcd").has_ok());
    EXPECT_EQ(finishUpload(*alice, t.upload_ticket().attachment_id(), sha256("abce")).error().code(),
        proto::ERROR_BAD_REQUEST);

    // Another connection cannot feed someone else's upload.
    t = beginUpload(*alice, general, "c.txt", 4);
    EXPECT_EQ(uploadChunk(*bob, t.upload_ticket().attachment_id(), 0, "abcd").error().code(), proto::ERROR_NOT_FOUND);

    // Users without ATTACH_FILES are refused.
    std::uint64_t bobId = 0;
    for (const auto& u : state.users())
        if (u.username() == "bob")
            bobId = u.id();
    proto::Envelope ov;
    auto* o = ov.mutable_set_override()->mutable_override();
    o->set_channel_id(general);
    o->set_target_type(proto::PermissionOverride::TARGET_USER);
    o->set_target_id(bobId);
    o->set_deny(permissions::AttachFiles);
    ASSERT_TRUE(alice->call(ov)->has_ok());
    EXPECT_EQ(beginUpload(*bob, general, "d.txt", 4).error().code(), proto::ERROR_PERMISSION_DENIED);
}

TEST_F(Fixture, InterruptedUploadsResumeOnlyForTheirOwner)
{
    auto alice = client("alice");
    auto bob = client("bob");
    const auto sid = createServer(*alice, "Drop");
    ASSERT_TRUE(join(*bob, invite(*alice, sid)).has_server());
    const auto general = channelNamed(sync(*alice), "general", proto::CHANNEL_TYPE_TEXT);
    const auto t = beginUpload(*alice, general, "partial.bin", 8);
    ASSERT_TRUE(t.has_upload_ticket());
    const auto id = t.upload_ticket().attachment_id();
    ASSERT_TRUE(uploadChunk(*alice, id, 0, "1234").has_ok());
    alice->abort();

    auto resume = [&](RawClient& c) {
        proto::Envelope env;
        env.mutable_resume_upload()->set_attachment_id(id);
        return c.call(env).value_or(proto::Envelope{});
    };
    auto again = client("alice");
    // Chunks are refused until the new connection claims the upload.
    EXPECT_EQ(uploadChunk(*again, id, 4, "5678").error().code(), proto::ERROR_NOT_FOUND);
    EXPECT_EQ(download(*again, id, 0).error().code(), proto::ERROR_NOT_FOUND) << "not finished yet";
    EXPECT_EQ(resume(*bob).error().code(), proto::ERROR_NOT_FOUND) << "someone else's upload";

    const auto ticket = resume(*again);
    ASSERT_TRUE(ticket.has_upload_ticket()) << ticket.error().message();
    EXPECT_EQ(ticket.upload_ticket().received(), 4u);
    ASSERT_TRUE(uploadChunk(*again, id, 4, "5678").has_ok());
    const auto done = finishUpload(*again, id, sha256("12345678"));
    ASSERT_TRUE(done.has_attachment()) << done.error().message();
    EXPECT_EQ(done.attachment().size(), 8u);

    // A second interrupted upload can be cancelled from the new connection.
    const auto t2 = beginUpload(*again, general, "other.bin", 8);
    ASSERT_TRUE(t2.has_upload_ticket());
    again->abort();
    auto third = client("alice");
    proto::Envelope cancel;
    cancel.mutable_cancel_upload()->set_attachment_id(t2.upload_ticket().attachment_id());
    EXPECT_TRUE(third->call(cancel)->has_ok());
}

TEST_F(Fixture, ServerWideSearchSkipsChannelsYouCannotRead)
{
    auto alice = client("alice");
    auto bob = client("bob");
    auto stranger = client("mallory");
    const auto sid = createServer(*alice, "Search");
    ASSERT_TRUE(join(*bob, invite(*alice, sid)).has_server());

    proto::Envelope mk;
    mk.mutable_create_channel()->set_server_id(sid);
    mk.mutable_create_channel()->set_name("secret");
    auto secretReply = alice->call(mk);
    ASSERT_TRUE(secretReply && secretReply->has_channel());
    const auto secret = secretReply->channel().id();
    auto state = sync(*alice);
    const auto general = channelNamed(state, "general", proto::CHANNEL_TYPE_TEXT);
    std::uint64_t bobId = 0;
    for (const auto& u : state.users())
        if (u.username() == "bob")
            bobId = u.id();
    ASSERT_TRUE(general && bobId);

    proto::Envelope hide;
    auto* o = hide.mutable_set_override()->mutable_override();
    o->set_channel_id(secret);
    o->set_target_type(proto::PermissionOverride::TARGET_USER);
    o->set_target_id(bobId);
    o->set_deny(permissions::ViewChannel);
    ASSERT_TRUE(alice->call(hide)->has_ok());

    ASSERT_TRUE(send(*alice, general, "a unicorn in general").has_chat_message());
    ASSERT_TRUE(send(*alice, secret, "a unicorn in secret").has_chat_message());

    auto search = [&](RawClient& c) {
        proto::Envelope env;
        env.mutable_search_messages()->set_server_id(sid);
        env.mutable_search_messages()->set_query("unicorn");
        return c.call(env).value_or(proto::Envelope{});
    };
    const auto mine = search(*alice);
    ASSERT_TRUE(mine.has_message_page());
    EXPECT_EQ(mine.message_page().messages_size(), 2);

    const auto his = search(*bob);
    ASSERT_TRUE(his.has_message_page());
    ASSERT_EQ(his.message_page().messages_size(), 1);
    EXPECT_EQ(his.message_page().messages(0).channel_id(), general);

    EXPECT_EQ(search(*stranger).error().code(), proto::ERROR_NOT_FOUND);
}

TEST_F(Fixture, GroupConversationsAddRenameAndLeave)
{
    auto alice = client("alice");
    auto bob = client("bob");
    auto carol = client("carol");
    auto dave = client("dave");
    auto stranger = client("mallory");
    const auto sid = createServer(*alice, "Group");
    const auto token = invite(*alice, sid);
    for (auto* c : {bob.get(), carol.get(), dave.get()})
        ASSERT_TRUE(join(*c, token).has_server());
    std::map<std::string, std::uint64_t> ids;
    for (auto* c : {alice.get(), stranger.get()}) {
        const auto state = sync(*c);
        for (const auto& u : state.users())
            ids[u.username()] = u.id();
    }

    auto create = [&](std::vector<std::uint64_t> users, const std::string& name = {}) {
        proto::Envelope env;
        for (auto u : users)
            env.mutable_create_group_dm()->add_user_ids(u);
        env.mutable_create_group_dm()->set_name(name);
        return alice->call(env).value_or(proto::Envelope{});
    };
    EXPECT_EQ(create({ids["bob"]}).error().code(), proto::ERROR_BAD_REQUEST) << "two people is a DM";
    EXPECT_EQ(create({ids["bob"], ids["mallory"]}).error().code(), proto::ERROR_PERMISSION_DENIED);

    const auto made = create({ids["bob"], ids["carol"]}, "Plans");
    ASSERT_TRUE(made.has_channel()) << made.error().message();
    const auto& group = made.channel();
    EXPECT_EQ(group.type(), proto::CHANNEL_TYPE_GROUP_DM);
    EXPECT_EQ(group.name(), "Plans");
    EXPECT_EQ(group.recipient_ids_size(), 3);
    EXPECT_TRUE(carol->waitEvent([](const proto::Event& e) { return e.has_channel_create(); }));

    ASSERT_TRUE(send(*bob, group.id(), "hi group").has_chat_message());
    EXPECT_TRUE(carol->waitEvent([](const proto::Event& e) { return e.has_message_create(); }));
    EXPECT_EQ(send(*dave, group.id(), "let me in").error().code(), proto::ERROR_NOT_FOUND);

    // Carol adds Dave; he gets the conversation and can read its history.
    proto::Envelope add;
    add.mutable_add_group_dm_recipient()->set_channel_id(group.id());
    add.mutable_add_group_dm_recipient()->set_user_id(ids["dave"]);
    auto added = carol->call(add);
    ASSERT_TRUE(added && added->has_channel());
    EXPECT_EQ(added->channel().recipient_ids_size(), 4);
    EXPECT_TRUE(dave->waitEvent([](const proto::Event& e) { return e.has_channel_create(); }));
    proto::Envelope hist;
    hist.mutable_get_messages()->set_channel_id(group.id());
    auto page = dave->call(hist);
    ASSERT_TRUE(page && page->has_message_page());
    EXPECT_EQ(page->message_page().messages_size(), 1);

    // Any participant may rename it, but group DMs have no topic.
    proto::Envelope rename;
    rename.mutable_update_channel()->set_channel_id(group.id());
    rename.mutable_update_channel()->set_name("Weekend plans");
    auto renamed = dave->call(rename);
    ASSERT_TRUE(renamed && renamed->has_channel());
    EXPECT_EQ(renamed->channel().name(), "Weekend plans");
    proto::Envelope topic;
    topic.mutable_update_channel()->set_channel_id(group.id());
    topic.mutable_update_channel()->set_topic("x");
    topic.mutable_update_channel()->set_set_topic(true);
    EXPECT_EQ(dave->call(topic)->error().code(), proto::ERROR_PERMISSION_DENIED);

    // Leaving: the leaver loses access, the rest are told.
    bob->clearEvents();
    proto::Envelope leave;
    leave.mutable_leave_group_dm()->set_channel_id(group.id());
    ASSERT_TRUE(alice->call(leave)->has_ok());
    EXPECT_TRUE(alice->waitEvent([](const proto::Event& e) { return e.has_channel_delete(); }));
    auto update = bob->waitEvent([](const proto::Event& e) { return e.has_channel_update(); });
    ASSERT_TRUE(update);
    EXPECT_EQ(update->channel_update().recipient_ids_size(), 3);
    EXPECT_EQ(send(*alice, group.id(), "back?").error().code(), proto::ERROR_NOT_FOUND);

    for (auto* c : {bob.get(), carol.get(), dave.get()})
        ASSERT_TRUE(c->call(leave)->has_ok());
    EXPECT_EQ(send(*bob, group.id(), "anyone?").error().code(), proto::ERROR_NOT_FOUND) << "last one out deletes it";
}
