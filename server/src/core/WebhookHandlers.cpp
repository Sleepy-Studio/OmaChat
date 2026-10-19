#include "auth/Credentials.hpp"
#include "core/ChatServer.hpp"
#include "omachat/core/Validation.hpp"
#include <QJsonDocument>
#include <QJsonObject>
namespace omachat::server {
using namespace omachat::permissions;
bool ChatServer::manageWebhook(Session& s, std::uint64_t rid, Id channelId)
{
    const auto* c = m_state.channel(channelId);
    if (!m_config.webhooksEnabled) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("incoming webhooks are disabled"));
        return false;
    }
    if (!c || c->kind != ChannelKind::Text || !m_state.can(channelId, s.userId, ViewChannel | ManageChannel)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("channel management permission required"));
        return false;
    }
    return true;
}
void ChatServer::handleCreateWebhook(Session& s, std::uint64_t rid, const proto::CreateWebhookRequest& m)
{
    if (!limit(s, rid, s.invites) || !manageWebhook(s, rid, m.channel_id()))
        return;
    const auto name = QString::fromStdString(m.name()).trimmed();
    bool valid = !name.isEmpty() && name.size() <= 64;
    for (const auto ch : name)
        valid = valid && ch.isPrint() && ch.category() != QChar::Other_Format;
    if (!valid || !m_state.can(m.channel_id(), s.userId, SendMessages)) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST,
            QStringLiteral("name must be 1-64 printable characters and posting permission is required"));
        return;
    }
    if (m_store.webhooks(m.channel_id()).size() >= 8) {
        replyError(s, rid, proto::ERROR_CONFLICT, QStringLiteral("at most eight webhooks per channel"));
        return;
    }
    const auto token = auth::randomToken();
    WebhookRecord record{m_ids.next(), m.channel_id(), s.userId, name, auth::tokenDigest(token)};
    if (!m_store.createWebhook(record)) {
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not store webhook"));
        return;
    }
    proto::Envelope env;
    auto* out = env.mutable_incoming_webhook();
    out->set_id(record.id);
    out->set_channel_id(record.channelId);
    out->set_name(record.name.toStdString());
    out->set_token(token.toStdString());
    reply(s, rid, std::move(env));
}
void ChatServer::handleListWebhooks(Session& s, std::uint64_t rid, const proto::ListWebhooksRequest& m)
{
    if (!limit(s, rid, s.history) || !manageWebhook(s, rid, m.channel_id()))
        return;
    proto::Envelope env;
    auto* list = env.mutable_webhook_list();
    for (const auto& record : m_store.webhooks(m.channel_id())) {
        auto* out = list->add_webhooks();
        out->set_id(record.id);
        out->set_channel_id(record.channelId);
        out->set_name(record.name.toStdString());
    }
    reply(s, rid, std::move(env));
}
void ChatServer::handleRevokeWebhook(Session& s, std::uint64_t rid, const proto::RevokeWebhookRequest& m)
{
    if (!manageWebhook(s, rid, m.channel_id()))
        return;
    if (!m_store.revokeWebhook(m.channel_id(), m.webhook_id())) {
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not revoke webhook"));
        return;
    }
    m_webhookRates.erase(m.webhook_id());
    replyOk(s, rid);
}
int ChatServer::receiveWebhook(const QString& token, const QByteArray& body)
{
    if (!m_webhookGlobal.tryConsume())
        return 429;
    const auto record = m_store.webhook(auth::tokenDigest(token));
    if (!record)
        return 401;
    const auto* c = m_state.channel(record->channelId);
    // Every delivery checks current channel and creator access. Tokens never bypass revocation or permissions.
    if (!c || c->kind != ChannelKind::Text || m_store.isSuspended(record->creatorId)
        || !m_state.can(c->id, record->creatorId, ViewChannel | SendMessages | ManageChannel))
        return 403;
    auto it = m_webhookRates.find(record->id);
    if (it == m_webhookRates.end()) {
        if (m_webhookRates.size() >= 4096)
            return 503;
        it = m_webhookRates.emplace(record->id, TokenBucket{5, 0.5}).first;
    }
    if (!it->second.tryConsume())
        return 429;
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return 400;
    const auto obj = doc.object();
    if (obj.size() != 1 || !obj.value(QStringLiteral("content")).isString())
        return 400;
    const auto content = validation::messageContent(obj.value(QStringLiteral("content")).toString());
    if (!content)
        return 400;
    MessageRecord msg;
    msg.id = m_ids.next();
    msg.channelId = c->id;
    msg.authorId = record->creatorId;
    msg.mentionSnapshot = MentionSnapshot{};
    msg.integrationId = record->id;
    msg.integrationName = record->name;
    // Legacy clients also display provenance. External text never creates mentions.
    msg.content = QStringLiteral("[Integration: %1]\n%2").arg(record->name, *content);
    if (msg.content.size() > validation::kMaxMessageLength)
        return 400;
    if (!m_store.insertMessage(msg))
        return 500;
    proto::Event event;
    *event.mutable_message_create() = toProto(msg, 0);
    publish(event, m_state.channelAudience(c->id));
    return 201;
}
} // namespace omachat::server
