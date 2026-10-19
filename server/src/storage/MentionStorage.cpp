#include "storage/Store.hpp"

#include <QSqlQuery>

namespace omachat::server {
namespace {
qint64 sqlId(Id id)
{
    return static_cast<qint64>(id);
}
} // namespace

bool Store::writeMentionSnapshot(Id messageId, const std::optional<MentionSnapshot>& snapshot)
{
    if (!exec(QStringLiteral("DELETE FROM message_attention WHERE message_id=?"), {sqlId(messageId)})
        || !exec(QStringLiteral("DELETE FROM message_classification WHERE message_id=?"), {sqlId(messageId)}))
        return false;
    if (!snapshot)
        return true;
    proto::MentionClassification classification;
    classification.set_version(1);
    classification.set_classified(true);
    for (const auto& [kind, targetId] : snapshot->targets) {
        auto* target = classification.add_targets();
        target->set_kind(kind);
        target->set_target_id(targetId);
    }
    if (!exec(QStringLiteral("INSERT INTO message_classification(message_id,targets) VALUES(?,?)"),
            {sqlId(messageId), QByteArray::fromStdString(classification.SerializeAsString())}))
        return false;
    for (const auto& [user, mask] : snapshot->recipients) {
        if (!mask || mask > 7
            || !exec(QStringLiteral("INSERT INTO message_attention(message_id,user_id,mask) VALUES(?,?,?)"),
                {sqlId(messageId), sqlId(user), mask}))
            return false;
    }
    return true;
}

std::optional<MentionSnapshot> Store::mentionSnapshot(Id messageId, Id viewerId)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT targets FROM message_classification WHERE message_id=?"));
    query.addBindValue(sqlId(messageId));
    if (!query.exec() || !query.next())
        return std::nullopt;
    proto::MentionClassification classification;
    if (!classification.ParseFromString(query.value(0).toByteArray().toStdString()) || classification.version() != 1
        || !classification.classified())
        return std::nullopt;
    MentionSnapshot snapshot;
    for (const auto& target : classification.targets())
        snapshot.targets.emplace_back(target.kind(), target.target_id());
    query.prepare(viewerId
            ? QStringLiteral("SELECT user_id,mask FROM message_attention WHERE message_id=? AND user_id=?")
            : QStringLiteral("SELECT user_id,mask FROM message_attention WHERE message_id=?"));
    query.addBindValue(sqlId(messageId));
    if (viewerId)
        query.addBindValue(sqlId(viewerId));
    if (!query.exec())
        return std::nullopt;
    while (query.next())
        snapshot.recipients[static_cast<Id>(query.value(0).toLongLong())] = query.value(1).toUInt();
    return snapshot;
}

bool Store::updateClassifiedMessage(const MessageRecord& message)
{
    if (!begin())
        return false;
    if (!updateMessage(message.id, message.content, message.editedAt, message.mentions, message.encrypted)
        || !writeMentionSnapshot(message.id, message.mentionSnapshot)) {
        rollback();
        return false;
    }
    if (commit())
        return true;
    rollback();
    return false;
}

std::optional<proto::AttentionSummary> Store::attentionSummary(Id userId, Id channelId)
{
    if (!begin())
        return std::nullopt;
    const auto unread = unreadSummary(userId, channelId);
    if (!unread) {
        rollback();
        return std::nullopt;
    }
    // Both aggregate statements share one SQLite read transaction. No bodies,
    // role membership, permissions or markers are changed or recomputed here.
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(R"(
        WITH marker AS (
            SELECT message_id,timestamp FROM read_markers WHERE user_id=:user AND channel_id=:channel
        ), classified AS (
            SELECT CASE WHEN c.message_id IS NOT NULL THEN COALESCE(a.mask,0)
                        WHEN m.encrypted IS NULL AND instr(' ' || m.mentions || ' ', :mention)>0 THEN 1
                        ELSE 0 END AS mask,
                   c.message_id IS NOT NULL AS complete
            FROM messages m
            LEFT JOIN message_classification c ON c.message_id=m.id
            LEFT JOIN message_attention a ON a.message_id=m.id AND a.user_id=:user
            WHERE m.channel_id=:channel AND (m.author_id<>:user OR m.integration_id<>0)
              AND (NOT EXISTS(SELECT 1 FROM marker)
                   OR (m.created_at,m.id)>(SELECT timestamp,message_id FROM marker))
        )
        SELECT mask,COUNT(*),MIN(complete) FROM classified GROUP BY mask
    )"));
    query.bindValue(QStringLiteral(":user"), sqlId(userId));
    query.bindValue(QStringLiteral(":channel"), sqlId(channelId));
    query.bindValue(QStringLiteral(":mention"), QStringLiteral(" %1 ").arg(userId));
    if (!query.exec()) {
        rollback();
        return std::nullopt;
    }
    proto::AttentionSummary result;
    *result.mutable_unread() = *unread;
    for (int mask = 1; mask <= 7; ++mask)
        result.add_mask_counts(0);
    bool complete = true;
    while (query.next()) {
        const auto mask = query.value(0).toUInt();
        if (mask >= 1 && mask <= 7)
            result.set_mask_counts(static_cast<int>(mask - 1), query.value(1).toULongLong());
        complete = complete && query.value(2).toBool();
    }
    result.set_classification_complete(complete);
    query.finish();
    if (!commit()) {
        rollback();
        return std::nullopt;
    }
    return result;
}

} // namespace omachat::server
