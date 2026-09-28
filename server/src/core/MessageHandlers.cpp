#include "core/ChatServer.hpp"
#include "omachat/core/Log.hpp"
#include "omachat/core/Validation.hpp"

#include <QRegularExpression>

namespace omachat::server {

using namespace omachat::permissions;

namespace {

bool isMessageChannel(const ChannelRecord& c)
{
    return c.kind == ChannelKind::Text || c.kind == ChannelKind::Dm || c.kind == ChannelKind::GroupDm;
}

} // namespace

std::vector<Id> ChatServer::extractMentions(Id channelId, const QString& content) const
{
    static const QRegularExpression re(
        QStringLiteral(R"((?:^|[^\w@])@([a-z0-9][a-z0-9_.\-]{1,31}))"), QRegularExpression::CaseInsensitiveOption);
    std::vector<Id> out;
    auto it = re.globalMatch(content);
    while (it.hasNext() && out.size() < 50) {
        const auto match = it.next();
        const UserRecord* u = m_state.userByName(match.captured(1).toLower());
        if (!u || std::ranges::find(out, u->id) != out.end())
            continue;
        if (m_state.can(channelId, u->id, ViewChannel))
            out.push_back(u->id);
    }
    return out;
}

void ChatServer::handleSendMessage(Session& s, std::uint64_t rid, const proto::SendMessageRequest& m)
{
    if (!limit(s, rid, s.messages))
        return;
    const ChannelRecord* c = m_state.channel(m.channel_id());
    if (!c || !m_state.can(c->id, s.userId, ViewChannel)) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("channel not found"));
        return;
    }
    if (!isMessageChannel(*c)) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("this channel does not accept messages"));
        return;
    }
    if (!m_state.can(c->id, s.userId, SendMessages)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot send messages here"));
        return;
    }
    if (m.attachment_ids_size() > 0) {
        replyError(
            s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("attachments are not supported by this server yet"));
        return;
    }
    const auto content = validation::messageContent(QString::fromStdString(m.content()));
    if (!content) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST,
            QStringLiteral("messages must be 1-%1 characters").arg(validation::kMaxMessageLength));
        return;
    }
    MessageRecord msg;
    msg.id = m_ids.next();
    msg.channelId = c->id;
    msg.authorId = s.userId;
    msg.content = *content;
    msg.isAction = m.is_action();
    if (m.reply_to()) {
        const auto parent = m_store.message(m.reply_to());
        if (parent && parent->channelId == c->id)
            msg.replyTo = parent->id;
    }
    msg.mentions = extractMentions(c->id, msg.content);
    if (!m_store.insertMessage(msg)) {
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not store message"));
        return;
    }
    proto::Event e;
    *e.mutable_message_create() = toProto(msg, 0);
    publish(e, m_state.channelAudience(c->id));

    proto::Envelope env;
    *env.mutable_chat_message() = e.message_create();
    reply(s, rid, std::move(env));
}

void ChatServer::handleEditMessage(Session& s, std::uint64_t rid, const proto::EditMessageRequest& m)
{
    if (!limit(s, rid, s.messages))
        return;
    auto msg = m_store.message(m.message_id());
    if (!msg || !m_state.can(msg->channelId, s.userId, ViewChannel)) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("message not found"));
        return;
    }
    if (msg->authorId != s.userId) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you can only edit your own messages"));
        return;
    }
    const auto content = validation::messageContent(QString::fromStdString(m.content()));
    if (!content) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST,
            QStringLiteral("messages must be 1-%1 characters").arg(validation::kMaxMessageLength));
        return;
    }
    msg->content = *content;
    msg->editedAt = now();
    msg->mentions = extractMentions(msg->channelId, msg->content);
    if (!m_store.updateMessage(msg->id, msg->content, msg->editedAt, msg->mentions)) {
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not update message"));
        return;
    }
    proto::Event e;
    *e.mutable_message_update() = toProto(*msg, 0);
    publish(e, m_state.channelAudience(msg->channelId));
    proto::Envelope env;
    *env.mutable_chat_message() = e.message_update();
    reply(s, rid, std::move(env));
}

void ChatServer::handleDeleteMessage(Session& s, std::uint64_t rid, const proto::DeleteMessageRequest& m)
{
    const auto msg = m_store.message(m.message_id());
    if (!msg || !m_state.can(msg->channelId, s.userId, ViewChannel)) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("message not found"));
        return;
    }
    if (msg->authorId != s.userId && !m_state.can(msg->channelId, s.userId, ManageMessages)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot delete this message"));
        return;
    }
    if (!m_store.deleteMessage(msg->id)) {
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not delete message"));
        return;
    }
    proto::Event e;
    auto* d = e.mutable_message_delete();
    d->set_channel_id(msg->channelId);
    d->set_message_id(msg->id);
    publish(e, m_state.channelAudience(msg->channelId));
    replyOk(s, rid);
}

void ChatServer::handleGetMessages(Session& s, std::uint64_t rid, const proto::GetMessagesRequest& m)
{
    if (!limit(s, rid, s.history))
        return;
    const ChannelRecord* c = m_state.channel(m.channel_id());
    if (!c || !m_state.can(c->id, s.userId, ViewChannel)) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("channel not found"));
        return;
    }
    if (!m_state.can(c->id, s.userId, ReadHistory)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot read history here"));
        return;
    }
    const int lim = m.limit() == 0 ? 50 : static_cast<int>(std::min<std::uint32_t>(m.limit(), 100));
    bool hasMore = false;
    const auto page = m_store.messagePage(c->id, m.before_message_id(), lim, &hasMore);
    proto::Envelope env;
    auto* p = env.mutable_message_page();
    p->set_channel_id(c->id);
    p->set_has_more(hasMore);
    for (const auto& msg : page)
        *p->add_messages() = toProto(msg, s.userId);
    reply(s, rid, std::move(env));
}

void ChatServer::handleSearch(Session& s, std::uint64_t rid, const proto::SearchMessagesRequest& m)
{
    if (!limit(s, rid, s.history))
        return;
    const ChannelRecord* c = m_state.channel(m.channel_id());
    if (!c || !m_state.can(c->id, s.userId, ViewChannel | ReadHistory)) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("channel not found"));
        return;
    }
    const QString query = QString::fromStdString(m.query()).trimmed();
    if (query.isEmpty() || query.size() > 200) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("search queries are 1-200 characters"));
        return;
    }
    const int lim = m.limit() == 0 ? 25 : static_cast<int>(std::min<std::uint32_t>(m.limit(), 50));
    proto::Envelope env;
    auto* p = env.mutable_message_page();
    p->set_channel_id(c->id);
    for (const auto& msg : m_store.searchMessages(c->id, query, lim))
        *p->add_messages() = toProto(msg, s.userId);
    reply(s, rid, std::move(env));
}

void ChatServer::handleReaction(Session& s, std::uint64_t rid, const proto::ReactionRequest& m)
{
    if (!limit(s, rid, s.messages))
        return;
    const auto msg = m_store.message(m.message_id());
    if (!msg || !m_state.can(msg->channelId, s.userId, ViewChannel)) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("message not found"));
        return;
    }
    if (!m_state.can(msg->channelId, s.userId, AddReactions)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot react here"));
        return;
    }
    const auto emoji = validation::reaction(QString::fromStdString(m.emoji()));
    if (!emoji) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("invalid reaction"));
        return;
    }
    if (m.add() && m_store.reactions(msg->id, 0).size() >= 20) {
        bool existing = false;
        for (const auto& r : m_store.reactions(msg->id, 0))
            existing = existing || r.emoji == *emoji;
        if (!existing) {
            replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("too many distinct reactions"));
            return;
        }
    }
    m_store.setReaction(msg->id, s.userId, *emoji, m.add());
    proto::Event e;
    auto* r = e.mutable_reaction();
    r->set_channel_id(msg->channelId);
    r->set_message_id(msg->id);
    r->set_emoji(emoji->toStdString());
    r->set_user_id(s.userId);
    r->set_add(m.add());
    publish(e, m_state.channelAudience(msg->channelId));
    replyOk(s, rid);
}

void ChatServer::handleTyping(Session& s, std::uint64_t rid, const proto::TypingRequest& m)
{
    const ChannelRecord* c = m_state.channel(m.channel_id());
    if (!c || !m_state.can(c->id, s.userId, SendMessages)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("cannot type here"));
        return;
    }
    // Excess typing notifications are dropped silently; they are advisory.
    if (s.typing.tryConsume()) {
        proto::Event e;
        auto* t = e.mutable_typing();
        t->set_channel_id(c->id);
        t->set_user_id(s.userId);
        publishEphemeral(e, m_state.channelAudience(c->id), s.userId);
    }
    replyOk(s, rid);
}

void ChatServer::handleSetPresence(Session& s, std::uint64_t rid, const proto::SetPresenceRequest& m)
{
    if (!limit(s, rid, s.presence))
        return;
    const auto status = m.status();
    if (status != proto::USER_STATUS_ONLINE && status != proto::USER_STATUS_IDLE
        && status != proto::USER_STATUS_DO_NOT_DISTURB) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("status must be online, idle or dnd"));
        return;
    }
    m_chosenStatus[s.userId] = status;
    proto::Event e;
    auto* p = e.mutable_presence_update();
    p->set_user_id(s.userId);
    p->set_status(status);
    const auto audience = m_state.audienceOf(s.userId);
    std::vector<Id> recipients(audience.begin(), audience.end());
    recipients.push_back(s.userId);
    publish(e, recipients);
    replyOk(s, rid);
}

} // namespace omachat::server
