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
    return QCryptographicHash::hash(QByteArrayView(data.data(), static_cast<qsizetype>(data.size())),
        QCryptographicHash::Sha256)
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
    EXPECT_EQ(uploadChunk(*alice, t.upload_ticket().attachment_id(), 2, "ab").error().code(),
        proto::ERROR_BAD_REQUEST);
    EXPECT_EQ(uploadChunk(*alice, t.upload_ticket().attachment_id(), 0, "abcd").error().code(),
        proto::ERROR_NOT_FOUND);

    t = beginUpload(*alice, general, "b.txt", 4);
    ASSERT_TRUE(uploadChunk(*alice, t.upload_ticket().attachment_id(), 0, "abcd").has_ok());
    EXPECT_EQ(finishUpload(*alice, t.upload_ticket().attachment_id(), sha256("abce")).error().code(),
        proto::ERROR_BAD_REQUEST);

    // Another connection cannot feed someone else's upload.
    t = beginUpload(*alice, general, "c.txt", 4);
    EXPECT_EQ(uploadChunk(*bob, t.upload_ticket().attachment_id(), 0, "abcd").error().code(),
        proto::ERROR_NOT_FOUND);

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

TEST_F(Fixture, UnfinishedUploadsDieWithTheirConnection)
{
    auto alice = client("alice");
    const auto sid = createServer(*alice, "Drop");
    const auto general = channelNamed(sync(*alice), "general", proto::CHANNEL_TYPE_TEXT);
    const auto t = beginUpload(*alice, general, "partial.bin", 8);
    ASSERT_TRUE(t.has_upload_ticket());
    ASSERT_TRUE(uploadChunk(*alice, t.upload_ticket().attachment_id(), 0, "1234").has_ok());
    alice->abort();

    auto again = client("alice");
    EXPECT_EQ(uploadChunk(*again, t.upload_ticket().attachment_id(), 4, "5678").error().code(),
        proto::ERROR_NOT_FOUND);
    EXPECT_EQ(download(*again, t.upload_ticket().attachment_id(), 0).error().code(), proto::ERROR_NOT_FOUND);
    (void)sid;
}
