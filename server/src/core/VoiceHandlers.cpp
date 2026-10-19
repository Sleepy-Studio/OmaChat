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
    const bool direct = c->kind == ChannelKind::Dm && directVoiceAllowed(s, c->id);
    if (c->kind != ChannelKind::Voice && !direct) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("not a voice channel"));
        return;
    }
    if (!direct && !m_state.can(c->id, s.userId, ConnectVoice)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot join this voice channel"));
        return;
    }
    for (const auto& [id, call] : m_calls) {
        if ((call.state.caller_id() == s.userId || call.state.callee_id() == s.userId) && !direct) {
            replyError(
                s, rid, proto::ERROR_CONFLICT, QStringLiteral("finish your direct call before joining channel voice"));
            return;
        }
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
        // An explicit same-room device transfer keeps the account's status.
        if (existing->second.channelId == c->id)
            v.statusText = existing->second.statusText;
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
    m_relay.setStreamFlags(v.streamId, (direct || m_state.can(c->id, s.userId, Speak)) && !v.selfMute && !v.serverMute,
        v.selfDeaf || v.serverDeaf);
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
    for (const auto& [id, call] : m_calls) {
        if ((call.state.caller_id() == s.userId || call.state.callee_id() == s.userId) && call.callerConn != s.connId
            && call.calleeConn != s.connId) {
            replyError(s, rid, proto::ERROR_CONFLICT,
                QStringLiteral("call belongs to another device; decline an incoming ring explicitly"));
            return;
        }
    }
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
    for (const auto& [id, call] : m_calls) {
        if (call.state.caller_id() == userId || call.state.callee_id() == userId) {
            endDirectCall(id, QStringLiteral("hangup"));
            return;
        }
    }
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
    const bool canSpeak = (directVoiceAllowed(s, v.channelId) || m_state.can(v.channelId, s.userId, Speak))
        && !v.selfMute && !v.serverMute;
    m_relay.setStreamFlags(v.streamId, canSpeak, v.selfDeaf || v.serverDeaf);
    publishVoiceState(s.userId, v, v.channelId);
    replyOk(s, rid);
}

void ChatServer::handleSetVoiceStatus(Session& s, std::uint64_t rid, const proto::SetVoiceStatusRequest& m)
{
    const auto it = m_voice.find(s.userId);
    if (it == m_voice.end()) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("not in a voice channel"));
        return;
    }
    if (it->second.ownerConnId != s.connId) {
        replyError(s, rid, proto::ERROR_CONFLICT, QStringLiteral("voice belongs to another device"));
        return;
    }
    const auto& bytes = m.text();
    if (bytes.size() > 320) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("voice status is limited to 80 characters"));
        return;
    }
    const auto text = QString::fromUtf8(bytes.data(), static_cast<qsizetype>(bytes.size()));
    bool valid = text.toUtf8().toStdString() == bytes && text.toUcs4().size() <= 80;
    for (const auto ch : text) {
        // Single-line plain text; reject control/bidi formatting characters.
        if (ch.category() == QChar::Other_Control || ch.category() == QChar::Other_Format
            || ch.category() == QChar::Separator_Line || ch.category() == QChar::Separator_Paragraph)
            valid = false;
    }
    if (!valid) {
        replyError(
            s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("voice status must be up to 80 single-line characters"));
        return;
    }
    auto& v = it->second;
    const auto next = text.trimmed();
    if (v.statusText != next) {
        v.statusText = next;
        publishVoiceState(s.userId, v, v.channelId);
    }
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
        const auto* room = m_state.channel(v.channelId);
        if (room && room->kind == ChannelKind::Dm && m_state.can(v.channelId, uid, ViewChannel))
            continue; // accepted direct-call participants; no guild permission overrides
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

void ChatServer::publishDirectCall(const CallRec& call)
{
    proto::Event event;
    *event.mutable_direct_call_changed() = call.state;
    // Ephemeral: a reconnect must never replay an obsolete ringing prompt.
    publishEphemeral(event, {call.state.caller_id(), call.state.callee_id()});
}

bool ChatServer::directVoiceAllowed(const Session& s, Id channel) const
{
    for (const auto& [id, call] : m_calls) {
        if (call.state.channel_id() == channel && call.state.state() == "accepted"
            && ((call.state.caller_id() == s.userId && call.callerConn == s.connId)
                || (call.state.callee_id() == s.userId && call.calleeConn == s.connId)))
            return true;
    }
    return false;
}

void ChatServer::handleStartDirectCall(Session& s, std::uint64_t rid, const proto::StartDirectCallRequest& m)
{
    const auto* room = m_state.channel(m.channel_id());
    if (!room || room->kind != ChannelKind::Dm || room->recipients.size() != 2
        || !m_state.can(room->id, s.userId, ViewChannel)) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("direct conversation not found"));
        return;
    }
    const Id other = room->recipients[0] == s.userId ? room->recipients[1] : room->recipients[0];
    if (!limit(s, rid, s.conversations))
        return;
    for (const auto& [id, call] : m_calls) {
        if (call.state.caller_id() == s.userId || call.state.callee_id() == s.userId || call.state.caller_id() == other
            || call.state.callee_id() == other) {
            replyError(s, rid, proto::ERROR_CONFLICT, QStringLiteral("busy: a participant already has a call"));
            return;
        }
    }
    if (m_voice.contains(s.userId) || m_voice.contains(other)) {
        replyError(s, rid, proto::ERROR_CONFLICT, QStringLiteral("busy: leave channel voice before calling"));
        return;
    }
    bool online = false;
    for (auto i = m_userConns.lower_bound(other); i != m_userConns.upper_bound(other); ++i) {
        const auto* target = sessionFor(i->second);
        if (target && target->phase == Session::Phase::Ready)
            online = true;
    }
    if (!online || statusOf(other) == proto::USER_STATUS_DO_NOT_DISTURB) {
        replyError(
            s, rid, proto::ERROR_CONFLICT, online ? QStringLiteral("busy: do not disturb") : QStringLiteral("offline"));
        return;
    }
    CallRec call;
    call.state.set_id(m_ids.next());
    call.state.set_channel_id(room->id);
    call.state.set_caller_id(s.userId);
    call.state.set_callee_id(other);
    call.state.set_caller_session_id(s.sessionId);
    call.state.set_state("ringing");
    call.callerConn = s.connId;
    const Id id = call.state.id();
    m_calls.emplace(id, call);
    proto::Envelope env;
    *env.mutable_direct_call() = call.state;
    reply(s, rid, std::move(env));
    publishDirectCall(call);
    QTimer::singleShot(std::chrono::seconds(30), this, [this, id] {
        const auto it = m_calls.find(id);
        if (it != m_calls.end() && it->second.state.state() == "ringing")
            endDirectCall(id, QStringLiteral("timeout"));
    });
}

void ChatServer::endDirectCall(Id id, const QString& reason)
{
    const auto it = m_calls.find(id);
    if (it == m_calls.end())
        return;
    auto call = it->second;
    m_calls.erase(it); // remove before leaveVoice to avoid recursive teardown
    call.state.set_state("ended");
    call.state.set_reason(reason.toStdString());
    publishDirectCall(call);
    leaveVoice(call.state.caller_id());
    leaveVoice(call.state.callee_id());
}

void ChatServer::disconnectDirectCalls(quint64 conn)
{
    std::vector<Id> ended;
    for (const auto& [id, call] : m_calls) {
        bool lastCallee = false;
        if (call.state.state() == "ringing") {
            lastCallee = true;
            for (auto i = m_userConns.lower_bound(call.state.callee_id());
                i != m_userConns.upper_bound(call.state.callee_id()); ++i)
                if (i->second != conn && sessionFor(i->second))
                    lastCallee = false;
        }
        if (call.callerConn == conn || call.calleeConn == conn || lastCallee)
            ended.push_back(id);
    }
    for (Id id : ended)
        endDirectCall(id, QStringLiteral("disconnected"));
}

void ChatServer::handleDirectCallAction(Session& s, std::uint64_t rid, const proto::DirectCallActionRequest& m)
{
    const auto it = m_calls.find(m.call_id());
    if (it == m_calls.end() || (it->second.state.caller_id() != s.userId && it->second.state.callee_id() != s.userId)) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("call not found or already ended"));
        return;
    }
    auto& call = it->second;
    const bool caller = call.state.caller_id() == s.userId;
    const bool ringing = call.state.state() == "ringing";
    if (m.action() == "accept" && !caller && ringing) {
        if (m_voice.contains(s.userId)) {
            replyError(s, rid, proto::ERROR_CONFLICT, QStringLiteral("leave channel voice before accepting"));
            return;
        }
        call.calleeConn = s.connId;
        call.state.set_callee_session_id(s.sessionId);
        call.state.set_state("accepted");
        publishDirectCall(call);
    } else if ((m.action() == "decline" && !caller && ringing)
        || (m.action() == "cancel" && caller && ringing && call.callerConn == s.connId)
        || (m.action() == "hangup" && !ringing && (caller ? call.callerConn : call.calleeConn) == s.connId)) {
        endDirectCall(m.call_id(), QString::fromStdString(m.action()));
    } else {
        replyError(s, rid, proto::ERROR_CONFLICT, QStringLiteral("call changed or belongs to another device"));
        return;
    }
    replyOk(s, rid);
}

} // namespace omachat::server
