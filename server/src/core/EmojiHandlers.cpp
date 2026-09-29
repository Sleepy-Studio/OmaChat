// Custom per-server emoji. Creation reuses an attachment already uploaded
// through the normal begin_upload/upload_chunk/finish_upload flow; this file
// only registers it as a named emoji and keeps the server's list in sync.

#include "core/ChatServer.hpp"
#include "omachat/core/Validation.hpp"

namespace omachat::server {

using namespace omachat::permissions;

namespace {
constexpr int kMaxEmojiPerServer = 50;
constexpr std::uint64_t kMaxEmojiBytes = 256 * 1024;
}

void ChatServer::handleCreateEmoji(Session& s, std::uint64_t rid, const proto::CreateEmojiRequest& m)
{
    const ServerRecord* srv = m_state.server(m.server_id());
    if (!srv || !m_state.canInServer(m.server_id(), s.userId, ManageEmoji)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot manage emoji here"));
        return;
    }
    const auto name = validation::emojiName(QString::fromStdString(m.name()));
    if (!name) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST,
            QStringLiteral("emoji names are 2-32 characters, letters/numbers/underscore only"));
        return;
    }
    if (m_store.emojiCount(srv->id) >= kMaxEmojiPerServer) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("this server's emoji limit has been reached"));
        return;
    }
    const auto att = m_store.attachment(m.attachment_id());
    if (!att || att->uploaderId != s.userId || att->messageId != 0) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("upload an image first"));
        return;
    }
    if (!att->mimeType.startsWith(u"image/")) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("custom emoji must be an image"));
        return;
    }
    if (att->size > kMaxEmojiBytes) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("custom emoji must be 256KB or smaller"));
        return;
    }

    EmojiRecord e{m_ids.next(), srv->id, *name, att->id, s.userId, now()};
    if (!m_store.insertEmoji(e)) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("an emoji with that name already exists"));
        return;
    }

    std::vector<Id> members;
    for (const auto& [uid, mem] : srv->members)
        members.push_back(uid);
    proto::Event ev;
    auto* ee = ev.mutable_emoji_changed();
    ee->set_server_id(srv->id);
    *ee->mutable_emoji() = toProto(e);
    ee->set_added(true);
    publish(ev, members);

    proto::Envelope env;
    *env.mutable_custom_emoji() = toProto(e);
    reply(s, rid, std::move(env));
}

void ChatServer::handleDeleteEmoji(Session& s, std::uint64_t rid, const proto::DeleteEmojiRequest& m)
{
    const auto e = m_store.emoji(m.emoji_id());
    if (!e || e->serverId != m.server_id()) {
        replyError(s, rid, proto::ERROR_NOT_FOUND, QStringLiteral("emoji not found"));
        return;
    }
    if (!m_state.canInServer(e->serverId, s.userId, ManageEmoji)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you cannot manage emoji here"));
        return;
    }
    const ServerRecord* srv = m_state.server(e->serverId);
    if (!m_store.deleteEmoji(e->id)) {
        replyError(s, rid, proto::ERROR_INTERNAL, QStringLiteral("could not delete emoji"));
        return;
    }

    std::vector<Id> members;
    if (srv) {
        for (const auto& [uid, mem] : srv->members)
            members.push_back(uid);
    }
    proto::Event ev;
    auto* ee = ev.mutable_emoji_changed();
    ee->set_server_id(e->serverId);
    *ee->mutable_emoji() = toProto(*e);
    ee->set_added(false);
    publish(ev, members);
    replyOk(s, rid);
}

void ChatServer::handleListEmoji(Session& s, std::uint64_t rid, const proto::ListEmojiRequest& m)
{
    if (!m_state.canInServer(m.server_id(), s.userId, ViewChannel)) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("you are not a member of this server"));
        return;
    }
    proto::Envelope env;
    auto* list = env.mutable_emoji_list();
    list->set_server_id(m.server_id());
    for (const auto& e : m_store.emojiFor(m.server_id()))
        *list->add_emoji() = toProto(e);
    reply(s, rid, std::move(env));
}

} // namespace omachat::server
