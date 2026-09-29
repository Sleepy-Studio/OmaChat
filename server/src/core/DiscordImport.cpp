#include "core/ChatServer.hpp"
#include "omachat/core/Validation.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>

#include <algorithm>

namespace omachat::server {
namespace {

bool discordId(const std::string& value)
{
    if (value.empty() || value.size() > 20)
        return false;
    for (char ch : value)
        if (ch < '0' || ch > '9')
            return false;
    return value != "0";
}

} // namespace

void ChatServer::handleImportDiscordBatch(Session& s, std::uint64_t rid, const proto::ImportDiscordBatchRequest& m)
{
    const ServerRecord* server = m_state.server(m.server_id());
    if (!server || server->ownerId != s.userId) {
        replyError(s, rid, proto::ERROR_PERMISSION_DENIED, QStringLiteral("only the server owner can import history"));
        return;
    }
    if (!discordId(m.guild_id()) || !discordId(m.channel_id())
        || (!m.category_id().empty() && !discordId(m.category_id())) || m.messages_size() > 50) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("invalid Discord import batch"));
        return;
    }
    auto name = validation::channelName(QString::fromStdString(m.channel_name()));
    const auto topic = validation::topic(QString::fromStdString(m.channel_topic()));
    if (!name || !topic) {
        replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("invalid channel name or topic"));
        return;
    }
    const auto mapped = [&](const char* kind, const std::string& source) {
        return m_store.discordImportId(m.server_id(), QLatin1StringView(kind), QString::fromStdString(source));
    };
    const auto remember = [&](const char* kind, const std::string& source, Id id) {
        return m_store.rememberDiscordImport(
            m.server_id(), QLatin1StringView(kind), QString::fromStdString(source), id);
    };
    const auto audience = [&] {
        std::vector<Id> users;
        for (const auto& [id, member] : m_state.server(m.server_id())->members)
            users.push_back(id);
        return users;
    };
    const auto publishChannel = [&](const ChannelRecord& c) {
        proto::Event event;
        *event.mutable_channel_create() = toProto(c, 0);
        publish(event, audience());
    };
    const auto fail = [&](const QString& reason) {
        replyError(s, rid, proto::ERROR_INTERNAL, reason);
    };

    Id parent = 0;
    if (!m.category_id().empty()) {
        if (auto found = mapped("category", m.category_id())) {
            parent = *found;
        } else {
            const auto categoryName = validation::channelName(QString::fromStdString(m.category_name()));
            if (!categoryName || server->channels.size() >= 500) {
                replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("invalid or excessive Discord categories"));
                return;
            }
            ChannelRecord category{m_ids.next(), m.server_id(), *categoryName, ChannelKind::Category, 0,
                static_cast<std::uint32_t>(server->channels.size()), {}, {}};
            if (!m_store.begin() || !m_store.insertChannel(category, now())
                || !remember("category", m.category_id(), category.id) || !m_store.commit()) {
                m_store.rollback();
                fail(QStringLiteral("could not store imported category"));
                return;
            }
            m_state.putChannel(category);
            parent = category.id;
            publishChannel(category);
        }
    }

    Id channelId = 0;
    if (auto found = mapped("channel", m.channel_id())) {
        channelId = *found;
        const ChannelRecord* existing = m_state.channel(channelId);
        if (!existing || existing->serverId != m.server_id()) {
            fail(QStringLiteral("imported channel mapping is stale"));
            return;
        }
    } else {
        if (m_state.server(m.server_id())->channels.size() >= 500) {
            replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("channel limit reached"));
            return;
        }
        name->replace(u' ', u'-');
        ChannelRecord channel{m_ids.next(), m.server_id(), name->toLower(), ChannelKind::Text, parent,
            static_cast<std::uint32_t>(m_state.server(m.server_id())->channels.size()), *topic, {}};
        if (!m_store.begin() || !m_store.insertChannel(channel, now())
            || !remember("channel", m.channel_id(), channel.id) || !m_store.commit()) {
            m_store.rollback();
            fail(QStringLiteral("could not store imported channel"));
            return;
        }
        m_state.putChannel(channel);
        channelId = channel.id;
        publishChannel(channel);
    }

    const auto authorFor = [&](const std::string& sourceId, const std::string& sourceName) -> Id {
        if (auto found = mapped("user", sourceId))
            return *found;
        const QString source = QString::fromStdString(sourceId);
        // An owner controls the export. Matching its claimed Discord ID to
        // a live OAuth identity would let that owner forge authorship.
        QString username = QStringLiteral("discord_%1").arg(source);
        Id userId = 0;
        for (int suffix = 0; suffix < 100 && !userId; ++suffix) {
            if (auto existing = m_store.userByName(username)) {
                if (!m_store.hasPassword(existing->id) && m_store.oauthIdentitiesForUser(existing->id).empty()) {
                    userId = existing->id;
                    break;
                }
                username = QStringLiteral("d_%1_%2").arg(source).arg(suffix);
                continue;
            }
            const auto display = validation::displayName(QString::fromStdString(sourceName));
            UserRecord user{m_ids.next(), username, display.value_or(QStringLiteral("Discord user")), {}, {}, now()};
            if (!m_store.insertUser(user))
                return 0;
            m_state.putUser(user);
            userId = user.id;
        }
        if (!userId || !remember("user", sourceId, userId))
            return 0;
        return userId;
    };
    const auto ensureMember = [&](Id author) {
        if (m_state.server(m.server_id())->members.contains(author))
            return true;
        MemberRecord member{m.server_id(), author, now(), {}};
        if (!m_store.insertMember(member))
            return false;
        m_state.putMember(member);
        proto::Event userEvent;
        *userEvent.mutable_user_update() = toProto(*m_state.user(author));
        publish(userEvent, audience());
        proto::Event memberEvent;
        *memberEvent.mutable_member_join() = toProto(member);
        publish(memberEvent, audience());
        return true;
    };

    std::uint32_t imported = 0;
    for (const auto& input : m.messages()) {
        if (!discordId(input.discord_id()) || !discordId(input.author_id())
            || (!input.reply_discord_id().empty() && !discordId(input.reply_discord_id()))
            || input.timestamp() < 1262304000000LL || input.timestamp() > now() + 86400000LL
            || input.content().size() > 65536 || input.assets_size() > 10 || input.mentions_size() > 50
            || input.reactions_size() > 100) {
            replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("invalid imported message"));
            return;
        }
        if (mapped("message", input.discord_id()))
            continue;
        const Id author = authorFor(input.author_id(), input.author_name());
        if (!author || !ensureMember(author)) {
            fail(QStringLiteral("could not store imported author"));
            return;
        }
        MessageRecord message;
        message.id = m_ids.next();
        message.channelId = channelId;
        message.authorId = author;
        message.content = QString::fromStdString(input.content());
        message.createdAt = input.timestamp();
        message.editedAt = input.edited_at();
        for (const auto& person : input.mentions()) {
            if (!discordId(person.id()))
                continue;
            const Id mentioned = authorFor(person.id(), person.name());
            if (!mentioned || !ensureMember(mentioned)) {
                fail(QStringLiteral("could not store imported mention"));
                return;
            }
            if (std::ranges::find(message.mentions, mentioned) == message.mentions.end())
                message.mentions.push_back(mentioned);
        }

        std::vector<Id> files;
        const auto cleanFiles = [&] {
            for (Id fileId : files) {
                m_store.deleteAttachment(fileId);
                QFile::remove(attachmentPath(fileId));
            }
        };
        for (const auto& asset : input.assets()) {
            const auto filename = validation::filename(QString::fromStdString(asset.filename()));
            if (!filename || asset.data().size() > 4 * 1024 * 1024) {
                cleanFiles();
                replyError(s, rid, proto::ERROR_BAD_REQUEST, QStringLiteral("invalid imported attachment"));
                return;
            }
            AttachmentRecord attachment;
            attachment.id = m_ids.next();
            attachment.channelId = channelId;
            attachment.uploaderId = author;
            attachment.filename = *filename;
            attachment.mimeType = validation::mimeType(QString::fromStdString(asset.mime_type()));
            attachment.size = static_cast<std::uint64_t>(asset.data().size());
            attachment.sha256 = QCryptographicHash::hash(QByteArray::fromStdString(asset.data()), QCryptographicHash::Sha256);
            attachment.createdAt = message.createdAt;
            const QString path = attachmentPath(attachment.id);
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(asset.data().data(), static_cast<qint64>(asset.data().size()))
                    != static_cast<qint64>(asset.data().size())
                || !file.flush() || !m_store.insertAttachment(attachment)) {
                QFile::remove(path);
                cleanFiles();
                fail(QStringLiteral("could not store imported attachment"));
                return;
            }
            files.push_back(attachment.id);
            message.attachments.push_back(attachment);
        }
        if (!m_store.insertMessage(message, m.server_id(), QString::fromStdString(input.discord_id()))) {
            cleanFiles();
            fail(QStringLiteral("could not store imported message"));
            return;
        }
        if (!input.reply_discord_id().empty()
            && !m_store.rememberDiscordReply(m.server_id(), message.id,
                QString::fromStdString(input.reply_discord_id()))) {
            fail(QStringLiteral("could not store imported reply"));
            return;
        }
        for (const auto& reaction : input.reactions()) {
            const auto emoji = validation::reaction(QString::fromStdString(reaction.emoji()));
            if (!emoji)
                continue;
            for (const auto& person : reaction.users()) {
                if (!discordId(person.id()))
                    continue;
                const Id user = authorFor(person.id(), person.name());
                if (!user || !ensureMember(user) || !m_store.setReaction(message.id, user, *emoji, true)) {
                    fail(QStringLiteral("could not store imported reaction"));
                    return;
                }
            }
        }
        ++imported;
    }
    if (!m_store.resolveDiscordReplies(m.server_id())) {
        fail(QStringLiteral("could not resolve imported replies"));
        return;
    }
    proto::Envelope replyEnv;
    auto* result = replyEnv.mutable_import_discord_result();
    result->set_channel_id(channelId);
    result->set_imported(imported);
    reply(s, rid, std::move(replyEnv));
}

} // namespace omachat::server
