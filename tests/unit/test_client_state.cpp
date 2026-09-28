#include "networking/ClientState.hpp"

#include <gtest/gtest.h>

using namespace omachat;
using namespace omachat::daemon;

namespace {

proto::SyncState baseSync()
{
    proto::SyncState s;
    auto* self = s.mutable_self();
    self->set_id(1);
    self->set_username("alice");
    self->set_display_name("Alice");
    auto* srv = s.add_servers();
    srv->set_id(100);
    srv->set_name("Sleepy Studio");
    srv->set_owner_id(1);
    auto add = [&](std::uint64_t id, const char* name, proto::ChannelType t) {
        auto* c = s.add_channels();
        c->set_id(id);
        c->set_server_id(100);
        c->set_name(name);
        c->set_type(t);
    };
    add(200, "general", proto::CHANNEL_TYPE_TEXT);
    add(201, "General", proto::CHANNEL_TYPE_VOICE);
    add(202, "development", proto::CHANNEL_TYPE_TEXT);
    auto* u = s.add_users();
    u->set_id(2);
    u->set_username("bob");
    u->set_display_name("Bob");
    s.set_last_sequence(10);
    return s;
}

} // namespace

TEST(ClientState, ResolvesChannelsPreferringExactMatchAndKind)
{
    ClientState st;
    st.reset(baseSync());
    EXPECT_EQ(st.resolveChannel(QStringLiteral("general")), 200u);
    EXPECT_EQ(st.resolveChannel(QStringLiteral("General")), 201u);
    EXPECT_EQ(st.resolveChannel(QStringLiteral("general"), ClientState::ChannelKind::Voice), 201u);
    EXPECT_EQ(st.resolveChannel(QStringLiteral("#General")), 200u) << "# implies a text channel";
    EXPECT_EQ(st.resolveChannel(QStringLiteral("Sleepy Studio/development")), 202u);
    EXPECT_EQ(st.resolveChannel(QStringLiteral("202")), 202u);
    EXPECT_EQ(st.resolveChannel(QStringLiteral("missing")), 0u);
    EXPECT_EQ(st.resolveUser(QStringLiteral("@BOB")), 2u);
    EXPECT_EQ(st.resolveServer(QStringLiteral("sleepy studio")), 100u);
}

TEST(ClientState, AppliesEventsAndTracksSequence)
{
    ClientState st;
    st.reset(baseSync());
    std::vector<ModelEvent> out;
    bool resync = false;

    proto::Event e;
    e.set_sequence(11);
    auto* v = e.mutable_voice_state_update();
    v->set_user_id(2);
    v->set_channel_id(201);
    st.apply(e, out, resync);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].name, QStringLiteral("voice.state"));
    EXPECT_EQ(st.voiceParticipants(201).size(), 1u);
    EXPECT_EQ(st.lastSequence(), 11u);

    proto::Event leave;
    leave.set_sequence(12);
    leave.mutable_voice_state_update()->set_user_id(2);
    st.apply(leave, out, resync);
    EXPECT_TRUE(st.voiceParticipants(201).empty());
    EXPECT_FALSE(resync);
}

TEST(ClientState, RemovalOfSelfDropsServer)
{
    ClientState st;
    st.reset(baseSync());
    std::vector<ModelEvent> out;
    bool resync = false;
    proto::Event e;
    e.mutable_member_leave()->set_server_id(100);
    e.mutable_member_leave()->set_user_id(1);
    e.mutable_member_leave()->set_reason("kicked");
    st.apply(e, out, resync);
    EXPECT_EQ(st.server(100), nullptr);
    EXPECT_EQ(st.channel(200), nullptr);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].name, QStringLiteral("server.removed"));
}

TEST(ClientState, StructuralEventsRequestResync)
{
    ClientState st;
    st.reset(baseSync());
    std::vector<ModelEvent> out;
    bool resync = false;
    proto::Event e;
    e.mutable_permissions_changed()->set_server_id(100);
    st.apply(e, out, resync);
    EXPECT_TRUE(resync);
}

TEST(ClientState, JsonUsesStringIdsAndFlagsMentions)
{
    ClientState st;
    st.reset(baseSync());
    proto::ChatMessage m;
    m.set_id(9007199254740993ULL); // not representable as a JS double
    m.set_channel_id(200);
    m.set_author_id(2);
    m.add_mention_ids(1);
    const QJsonObject j = st.messageJson(m);
    EXPECT_EQ(j.value("id").toString(), QStringLiteral("9007199254740993"));
    EXPECT_TRUE(j.value("mentions_me").toBool());
    EXPECT_EQ(idFromJson(j.value("id")), 9007199254740993ULL);
}
