#include "core/ChatServer.hpp"
#include "omachat/core/Log.hpp"
#include "omachat/core/Validation.hpp"

#include <QRegularExpression>

#include <set>

namespace omachat::server {

using namespace omachat::permissions;

namespace {

constexpr int kMaxAttachmentsPerMessage = 10;
// Sealed body plus one key wrap per participant device (10 people x 10 devices).
constexpr std::size_t kMaxEncryptedBytes = 64 * 1024;
constexpr int kMaxDeviceKeysPerUser = 10;

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

bool ChatServer::validEncrypted(
    const ChannelRecord& c, const std::string& content, const std::string& payload, Session& s, std::uint64_t rid)
{
    // Server channels stay readable by the server (search, moderation,
    // history for newcomers); only conversations are end-to-end.
    if (c.kind != ChannelKind::Dm && c.kind != ChannelKind::GroupDm) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("only conversations are end-to-end encrypted"));
        return false;
    }
    if (!content.empty() || payload.size() > kMaxEncryptedBytes) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("malformed encrypted message"));
        return false;
    }
    return true;
}

// ----------------------------------------------------------- device keys

void ChatServer::handlePublishDeviceKey(Session& s, std::uint64_t rid, const proto::PublishDeviceKeyRequest& m)
{
    if (!limit(s, rid, s.deviceKeys))
        return;
    if (m.public_key().size() != 32) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("device keys are 32 bytes"));
        return;
    }
    const QByteArray key = QByteArray::fromStdString(m.public_key());
    const auto existing = m_store.deviceKeys({s.userId});
    const bool known = std::ranges::any_of(existing, [&](const DeviceKeyRecord& k) { return k.publicKey == key; });
    if (!known && static_cast<int>(existing.size()) >= kMaxDeviceKeysPerUser) {
        replyError(s, rid, proto::ERROR_CONFLICT,
            QStringLiteral("%1 devices already have keys; remove an old account from one").arg(kMaxDeviceKeysPerUser));
        return;
    }
    if (!known) {
        if (!m_store.addDeviceKey({s.userId, key, now()})) {
            replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not store the device key"));
            return;
        }
        publishDeviceKeysChanged(s.userId);
    }
    replyOk(s, rid);
}

void ChatServer::handleRevokeDeviceKey(Session& s, std::uint64_t rid, const proto::RevokeDeviceKeyRequest& m)
{
    if (m_store.removeDeviceKey(s.userId, QByteArray::fromStdString(m.public_key())))
        publishDeviceKeysChanged(s.userId);
    replyOk(s, rid);
}

void ChatServer::handleGetDeviceKeys(Session& s, std::uint64_t rid, const proto::GetDeviceKeysRequest& m)
{
    if (!limit(s, rid, s.history))
        return;
    if (m.user_ids_size() > 50) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("at most 50 users per request"));
        return;
    }
    // Only people you could message: yourself and those you share a server or conversation with.
    const std::set<Id> audience = m_state.audienceOf(s.userId);
    std::vector<Id> users;
    for (Id uid : m.user_ids()) {
        if ((uid == s.userId || audience.contains(uid)) && std::ranges::find(users, uid) == users.end())
            users.push_back(uid);
    }
    proto::Envelope env;
    auto* list = env.mutable_device_key_list();
    for (const auto& k : m_store.deviceKeys(users)) {
        auto* p = list->add_keys();
        p->set_user_id(k.userId);
        p->set_public_key(k.publicKey.toStdString());
        p->set_created_at(k.createdAt);
    }
    reply(s, rid, std::move(env));
}

void ChatServer::publishDeviceKeysChanged(Id userId)
{
    proto::Event e;
    e.mutable_device_keys_changed()->set_user_id(userId);
    std::set<Id> audience = m_state.audienceOf(userId);
    audience.insert(userId); // their other devices
    publish(e, std::vector<Id>(audience.begin(), audience.end()));
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
    std::vector<AttachmentRecord> attachments;
    if (m.attachment_ids_size() > kMaxAttachmentsPerMessage) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST,
            QStringLiteral("at most %1 attachments per message").arg(kMaxAttachmentsPerMessage));
        return;
    }
    for (Id aid : m.attachment_ids()) {
        auto a = m_store.attachment(aid);
        if (!a || a->uploaderId != s.userId || a->channelId != c->id || a->messageId != 0
            || std::ranges::any_of(attachments, [aid](const auto& x) { return x.id == aid; })) {
            replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("attachment is not available to send here"));
            return;
        }
        attachments.push_back(std::move(*a));
    }
    if (!attachments.empty() && !m_state.can(c->id, s.userId, AttachFiles)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot attach files here"));
        return;
    }
    const bool encrypted = !m.encrypted().empty();
    if (encrypted && !validEncrypted(*c, m.content(), m.encrypted(), s, rid))
        return;
    // A message may be only attachments; otherwise it needs text.
    auto content = validation::messageContent(QString::fromStdString(m.content()));
    if (!content && (encrypted || !attachments.empty()) && QString::fromStdString(m.content()).trimmed().isEmpty())
        content = QString();
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
    msg.attachments = std::move(attachments);
    msg.encrypted = QByteArray::fromStdString(m.encrypted());
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
    const bool encrypted = !m.encrypted().empty();
    const ChannelRecord* c = m_state.channel(msg->channelId);
    if (encrypted && (!c || !validEncrypted(*c, m.content(), m.encrypted(), s, rid)))
        return;
    auto content = validation::messageContent(QString::fromStdString(m.content()));
    if (!content && (encrypted || !msg->attachments.empty()) && QString::fromStdString(m.content()).trimmed().isEmpty())
        content = QString();
    if (!content) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST,
            QStringLiteral("messages must be 1-%1 characters").arg(validation::kMaxMessageLength));
        return;
    }
    msg->content = *content;
    msg->editedAt = now();
    msg->mentions = extractMentions(msg->channelId, msg->content);
    msg->encrypted = QByteArray::fromStdString(m.encrypted());
    if (!m_store.updateMessage(msg->id, msg->content, msg->editedAt, msg->mentions, msg->encrypted)) {
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
    std::vector<Id> files;
    for (const auto& a : msg->attachments)
        files.push_back(a.id);
    removeAttachmentFiles(files);
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
    std::vector<Id> channels;
    if (m.channel_id() == 0 && m.server_id() != 0) {
        if (!m_state.member(m.server_id(), s.userId)) {
            replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("server not found"));
            return;
        }
        for (const auto& [id, c] : m_state.channels()) {
            if (c.serverId == m.server_id() && c.kind != ChannelKind::Category
                && m_state.can(id, s.userId, ViewChannel | ReadHistory))
                channels.push_back(id);
        }
    } else {
        const ChannelRecord* c = m_state.channel(m.channel_id());
        if (!c || !m_state.can(c->id, s.userId, ViewChannel | ReadHistory)) {
            replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("channel not found"));
            return;
        }
        channels.push_back(c->id);
    }
    const QString query = QString::fromStdString(m.query()).trimmed();
    if (query.isEmpty() || query.size() > 200) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("search queries are 1-200 characters"));
        return;
    }
    const int lim = m.limit() == 0 ? 25 : static_cast<int>(std::min<std::uint32_t>(m.limit(), 50));
    proto::Envelope env;
    auto* p = env.mutable_message_page();
    p->set_channel_id(m.channel_id());
    for (const auto& msg : m_store.searchMessages(channels, query, lim))
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
