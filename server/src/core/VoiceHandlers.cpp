#include "core/ChatServer.hpp"
#include "omachat/core/Log.hpp"

namespace omachat::server {

using namespace omachat::permissions;

namespace {
constexpr std::int64_t kVoiceSessionMs = 12LL * 3600 * 1000;
}

void ChatServer::publishVoiceState(Id userId, const VoiceRec& v, Id channelForAudience)
{
    proto::Event e;
    *e.mutable_voice_state_update() = toProto(userId, v);
    auto audience = m_state.channelAudience(channelForAudience);
    if (std::ranges::find(audience, userId) == audience.end())
        audience.push_back(userId);
    publish(std::move(e), std::move(audience));
}

void ChatServer::handleJoinVoice(Session& s, std::uint64_t rid, const proto::JoinVoiceRequest& m)
{
    const ChannelRecord* c = m_state.channel(m.channel_id());
    if (!c || !m_state.can(c->id, s.userId, ViewChannel)) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("channel not found"));
        return;
    }
    if (c->kind != ChannelKind::Voice) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("not a voice channel"));
        return;
    }
    if (!m_state.can(c->id, s.userId, ConnectVoice)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot join this voice channel"));
        return;
    }
    VoiceRec v;
    if (auto existing = m_voice.find(s.userId); existing != m_voice.end()) {
        if (existing->second.ownerSessionId != s.sessionId && !m.transfer()) {
            replyError(s, rid, proto::ERROR_CONFLICT,
                QStringLiteral("You are connected on another device. Move voice to this phone?"));
            return;
        }
        // Moderation belongs to the account, and survives ownership transfer.
        v.serverMute = existing->second.serverMute;
        v.serverDeaf = existing->second.serverDeaf;
        // Replace the relay stream directly. A transient leave event would make
        // the old client think it was evicted before learning who now owns it.
        m_relay.removeStream(existing->second.streamId);
    }
    v.ownerSessionId = s.sessionId;
    v.ownerConnId = s.connId;
    v.channelId = c->id;
    v.selfDeaf = m.self_deaf();
    v.selfMute = m.self_mute() || m.self_deaf();
    v.streamId = m_relay.allocateStreamId();

    MediaRelay::StreamInfo info;
    info.streamId = v.streamId;
    info.userId = s.userId;
    info.channelId = c->id;
    info.key = media::randomKey();
    info.expiresAtMs = now() + kVoiceSessionMs;
    m_relay.addStream(info);
    m_relay.setStreamFlags(
        v.streamId, m_state.can(c->id, s.userId, Speak) && !v.selfMute && !v.serverMute, v.selfDeaf || v.serverDeaf);
    m_relay.setVideoAllowed(v.streamId, false); // until SetStreaming
    m_voice[s.userId] = v;

    OMA_INFO("voice", "joined", {"user", s.userId}, {"channel", c->id}, {"stream", v.streamId});

    proto::Envelope env;
    auto* vs = env.mutable_voice_session();
    vs->set_channel_id(c->id);
    vs->set_stream_id(v.streamId);
    vs->set_media_key(std::string(reinterpret_cast<const char*>(info.key.data()), info.key.size()));
    vs->set_udp_port(mediaPort());
    vs->set_expires_at(info.expiresAtMs);
    vs->set_bitrate(static_cast<std::uint32_t>(m_config.voiceBitrate));
    reply(s, rid, std::move(env));

    publishVoiceState(s.userId, v, c->id);
}

void ChatServer::handleLeaveVoice(Session& s, std::uint64_t rid)
{
    const auto it = m_voice.find(s.userId);
    if (it != m_voice.end() && it->second.ownerConnId != s.connId) {
        replyError(s, rid, proto::ERROR_CONFLICT, QStringLiteral("voice belongs to another device"));
        return;
    }
    leaveVoice(s.userId);
    replyOk(s, rid);
}

void ChatServer::leaveVoice(Id userId)
{
    auto it = m_voice.find(userId);
    if (it == m_voice.end())
        return;
    VoiceRec old = it->second;
    m_voice.erase(it);
    m_relay.removeStream(old.streamId);
    OMA_INFO("voice", "left", {"user", userId}, {"channel", old.channelId});
    VoiceRec gone;
    publishVoiceState(userId, gone, old.channelId);
}

void ChatServer::handleSetVoiceState(Session& s, std::uint64_t rid, const proto::SetVoiceStateRequest& m)
{
    auto it = m_voice.find(s.userId);
    if (it == m_voice.end()) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("not in a voice channel"));
        return;
    }
    if (it->second.ownerConnId != s.connId) {
        replyError(s, rid, proto::ERROR_CONFLICT, QStringLiteral("voice belongs to another device"));
        return;
    }
    VoiceRec& v = it->second;
    v.selfDeaf = m.self_deaf();
    v.selfMute = m.self_mute() || m.self_deaf(); // deafen also stops transmitting
    const bool canSpeak = m_state.can(v.channelId, s.userId, Speak) && !v.selfMute && !v.serverMute;
    m_relay.setStreamFlags(v.streamId, canSpeak, v.selfDeaf || v.serverDeaf);
    publishVoiceState(s.userId, v, v.channelId);
    replyOk(s, rid);
}

void ChatServer::handleSetStreaming(Session& s, std::uint64_t rid, const proto::SetStreamingRequest& m)
{
    auto it = m_voice.find(s.userId);
    if (it == m_voice.end()) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("join a voice channel to share your screen"));
        return;
    }
    if (it->second.ownerConnId != s.connId) {
        replyError(s, rid, proto::ERROR_CONFLICT, QStringLiteral("voice belongs to another device"));
        return;
    }
    VoiceRec& v = it->second;
    if (m.streaming() && !m_state.can(v.channelId, s.userId, Stream)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot share your screen here"));
        return;
    }
    if (v.streaming != m.streaming()) {
        v.streaming = m.streaming();
        m_relay.setVideoAllowed(v.streamId, v.streaming);
        if (!v.streaming)
            m_relay.clearViewers(v.streamId);
        OMA_INFO("voice", v.streaming ? "stream started" : "stream stopped", {"user", s.userId});
        publishVoiceState(s.userId, v, v.channelId);
    }
    replyOk(s, rid);
}

void ChatServer::handleWatchStream(Session& s, std::uint64_t rid, const proto::WatchStreamRequest& m)
{
    auto viewer = m_voice.find(s.userId);
    auto source = m_voice.find(m.user_id());
    // Watching needs you in the same voice channel as the streamer.
    if (viewer == m_voice.end() || source == m_voice.end() || source->first == s.userId
        || source->second.channelId != viewer->second.channelId || (m.watch() && !source->second.streaming)) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("that user is not sharing in your voice channel"));
        return;
    }
    if (viewer->second.ownerConnId != s.connId) {
        replyError(s, rid, proto::ERROR_CONFLICT, QStringLiteral("voice belongs to another device"));
        return;
    }
    m_relay.setSubscription(viewer->second.streamId, source->second.streamId, m.watch());
    if (m.watch())
        m_relay.requestKeyframe(source->second.streamId);
    replyOk(s, rid);
}

void ChatServer::handleServerMute(Session& s, std::uint64_t rid, const proto::ServerMuteRequest& m)
{
    auto it = m_voice.find(m.user_id());
    const ChannelRecord* c = it != m_voice.end() ? m_state.channel(it->second.channelId) : nullptr;
    if (!c || c->serverId != m.server_id()) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("that user is not in voice on this server"));
        return;
    }
    if (!m_state.can(c->id, s.userId, MuteMembers)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot mute members"));
        return;
    }
    if (m.user_id() != s.userId && m_state.rank(c->serverId, m.user_id()) >= m_state.rank(c->serverId, s.userId)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("that member outranks you"));
        return;
    }
    VoiceRec& v = it->second;
    v.serverMute = m.mute();
    v.serverDeaf = m.deaf();
    const bool canSpeak = m_state.can(v.channelId, m.user_id(), Speak) && !v.selfMute && !v.serverMute;
    m_relay.setStreamFlags(v.streamId, canSpeak, v.selfDeaf || v.serverDeaf);
    publishVoiceState(m.user_id(), v, v.channelId);
    replyOk(s, rid);
}

void ChatServer::refreshVoicePermissions()
{
    std::vector<Id> evicted;
    for (auto& [uid, v] : m_voice) {
        if (!m_state.can(v.channelId, uid, ConnectVoice)) {
            evicted.push_back(uid);
            continue;
        }
        const bool canSpeak = m_state.can(v.channelId, uid, Speak) && !v.selfMute && !v.serverMute;
        m_relay.setStreamFlags(v.streamId, canSpeak, v.selfDeaf || v.serverDeaf);
        if (v.streaming && !m_state.can(v.channelId, uid, Stream)) {
            v.streaming = false;
            m_relay.clearViewers(v.streamId);
            publishVoiceState(uid, v, v.channelId);
        }
        m_relay.setVideoAllowed(v.streamId, v.streaming);
    }
    for (Id uid : evicted)
        leaveVoice(uid);
}

} // namespace omachat::server
