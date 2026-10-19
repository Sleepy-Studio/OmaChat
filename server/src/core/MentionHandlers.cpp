#include "core/ChatServer.hpp"
#include "core/MentionTokens.hpp"

namespace omachat::server {

using namespace omachat::permissions;

bool ChatServer::classifyMentions(
    Session& session, std::uint64_t rid, const ChannelRecord& channel, MessageRecord& message)
{
    message.mentionSnapshot.reset();
    message.mentions.clear();
    // Opaque content cannot establish any server-authoritative reason.
    if (!message.encrypted.isEmpty())
        return true;
    const auto broad = broadMentionTokens(message.content);
    if (!broad.empty() && (channel.kind != ChannelKind::Text || !channel.serverId)) {
        replyError(
            session, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("broadcast mentions require a server text channel"));
        return false;
    }
    message.mentions = extractMentions(channel.id, message.content);
    if (message.mentions.size() + broad.size() > 50) {
        replyError(session, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("at most 50 distinct mention targets"));
        return false;
    }
    MentionSnapshot snapshot;
    for (Id individual : message.mentions) {
        snapshot.targets.emplace_back(1, individual);
        if (individual != message.authorId)
            snapshot.recipients[individual] |= 1;
    }
    // Validate every target and permission before constructing or persisting broad recipients.
    for (const auto& [kind, id] : broad) {
        if (kind == 2) {
            const auto* role = m_state.role(id);
            if (!role || role->serverId != channel.serverId || role->isDefault) {
                replyError(session, rid, proto::ERROR_NOT_FOUND, QStringLiteral("mention target not found"));
                return false;
            }
        }
        if (!m_state.can(channel.id, session.userId, kind == 2 ? MentionRoles : MentionEveryone)) {
            replyError(session, rid, proto::ERROR_PERMISSION_DENIED,
                QStringLiteral("you cannot use this broadcast mention here"));
            return false;
        }
    }
    const auto* server = m_state.server(channel.serverId);
    for (const auto& [kind, id] : broad) {
        snapshot.targets.emplace_back(kind, id);
        if (!server)
            continue;
        for (const auto& [user, member] : server->members) {
            if (user == message.authorId || (kind == 2 && !member.roles.contains(id)))
                continue;
            if (m_state.can(channel.id, user, ViewChannel))
                snapshot.recipients[user] |= kind;
        }
    }
    message.mentionSnapshot = std::move(snapshot);
    return true;
}

void ChatServer::publishMessage(proto::Event event, const MessageRecord& message)
{
    const auto audience = m_state.channelAudience(message.channelId);
    const auto reasons = message.mentionSnapshot ? message.mentionSnapshot->recipients : std::map<Id, unsigned>{};
    // EventLog retains this revision's immutable recipient reasons, including on edits.
    m_events.append(event, audience, reasons);
    for (Id user : audience)
        deliver(user, EventLog::project(event, user, reasons));
}

void ChatServer::handleGetAttentionSummary(
    Session& session, std::uint64_t rid, const proto::GetAttentionSummaryRequest& request)
{
    if (!limit(session, rid, session.history))
        return;
    const auto* channel = m_state.channel(request.channel_id());
    if (!channel || !m_state.can(channel->id, session.userId, ViewChannel | ReadHistory)) {
        replyError(session, rid, proto::ERROR_NOT_FOUND, QStringLiteral("channel not found"));
        return;
    }
    if (channel->kind != ChannelKind::Text && channel->kind != ChannelKind::Dm
        && channel->kind != ChannelKind::GroupDm) {
        replyError(session, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("this channel does not accept messages"));
        return;
    }
    const auto summary = m_store.attentionSummary(session.userId, channel->id);
    if (!summary) {
        replyError(session, rid, proto::ERROR_INTERNAL, QStringLiteral("could not read attention state"));
        return;
    }
    proto::Envelope envelope;
    *envelope.mutable_attention_summary() = *summary;
    envelope.mutable_attention_summary()->mutable_unread()->set_sequence(m_events.lastSequence());
    reply(session, rid, std::move(envelope));
}

} // namespace omachat::server
