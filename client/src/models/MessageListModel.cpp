#include "models/MessageListModel.hpp"

#include "text/MarkdownRenderer.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QLocale>

namespace omachat::client {

namespace {
constexpr qint64 kGroupWindowMs = 5 * 60 * 1000;

qint64 ts(const QJsonObject& m)
{
    return static_cast<qint64>(m.value(QStringLiteral("timestamp")).toDouble());
}
QString sid(const QJsonObject& m, const char* key)
{
    return m.value(QLatin1StringView(key)).toString();
}
} // namespace

MessageListModel::MessageListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int MessageListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_items.size());
}

QHash<int, QByteArray> MessageListModel::roleNames() const
{
    return {{IdRole, "messageId"}, {AuthorIdRole, "authorId"}, {AuthorNameRole, "authorName"},
        {AuthorColorRole, "authorColor"}, {ContentRole, "content"}, {HtmlRole, "html"}, {TimestampRole, "timestamp"},
        {TimeTextRole, "timeText"}, {DayTextRole, "dayText"}, {EditedRole, "edited"}, {IsActionRole, "isAction"},
        {ReplyToRole, "replyTo"}, {ReplyPreviewRole, "replyPreview"}, {MentionsMeRole, "mentionsMe"},
        {ReactionsRole, "reactions"}, {GroupStartRole, "groupStart"}, {DayStartRole, "dayStart"}, {IsOwnRole, "isOwn"}};
}

QVariant MessageListModel::data(const QModelIndex& index, int role) const
{
    const int r = index.row();
    if (r < 0 || r >= m_items.size())
        return {};
    const Item& it = m_items.at(r);
    const QJsonObject& m = it.json;
    const QString author = sid(m, "author_id");
    // Row r+1 is the previous (older) message.
    const QJsonObject* older = r + 1 < m_items.size() ? &m_items.at(r + 1).json : nullptr;
    const QDateTime when = QDateTime::fromMSecsSinceEpoch(ts(m));

    switch (role) {
    case IdRole:
        return sid(m, "id");
    case AuthorIdRole:
        return author;
    case AuthorNameRole:
        return m_hooks.displayName ? m_hooks.displayName(author) : author;
    case AuthorColorRole:
        return m_hooks.nameColor ? m_hooks.nameColor(author) : QString();
    case ContentRole:
        return m.value(QStringLiteral("content")).toString();
    case HtmlRole:
        return it.html;
    case TimestampRole:
        return static_cast<double>(ts(m));
    case TimeTextRole:
        return QLocale().toString(when.time(), QLocale::ShortFormat);
    case DayTextRole:
        return QLocale().toString(when.date(), QLocale::LongFormat);
    case EditedRole:
        return m.value(QStringLiteral("edited_at")).toDouble() > 0;
    case IsActionRole:
        return m.value(QStringLiteral("is_action")).toBool();
    case ReplyToRole: {
        const QString rt = sid(m, "reply_to");
        return rt == u"0" ? QString() : rt;
    }
    case ReplyPreviewRole: {
        const QString rt = sid(m, "reply_to");
        if (rt.isEmpty() || rt == u"0")
            return QString();
        const int row = rowOf(rt);
        if (row < 0)
            return tr("Reply to an earlier message");
        const QJsonObject& p = m_items.at(row).json;
        const QString who = m_hooks.displayName ? m_hooks.displayName(sid(p, "author_id")) : QString();
        return who + QStringLiteral(": ")
            + MarkdownRenderer::plainPreview(p.value(QStringLiteral("content")).toString(), 90);
    }
    case MentionsMeRole:
        return m.value(QStringLiteral("mentions_me")).toBool();
    case ReactionsRole:
        return m.value(QStringLiteral("reactions")).toArray().toVariantList();
    case GroupStartRole: {
        if (!older)
            return true;
        const bool reply = !sid(m, "reply_to").isEmpty() && sid(m, "reply_to") != u"0";
        return reply || m.value(QStringLiteral("is_action")).toBool() || sid(*older, "author_id") != author
            || ts(m) - ts(*older) > kGroupWindowMs || QDateTime::fromMSecsSinceEpoch(ts(*older)).date() != when.date();
    }
    case DayStartRole:
        return !older || QDateTime::fromMSecsSinceEpoch(ts(*older)).date() != when.date();
    case IsOwnRole:
        return m_hooks.selfId && author == m_hooks.selfId();
    }
    return {};
}

QString MessageListModel::idAt(int row) const
{
    return sid(m_items.at(row).json, "id");
}

int MessageListModel::rowOf(const QString& id) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (idAt(i) == id)
            return i;
    }
    return -1;
}

QVariantMap MessageListModel::get(int row) const
{
    if (row < 0 || row >= m_items.size())
        return {};
    QVariantMap v = m_items.at(row).json.toVariantMap();
    v.insert(QStringLiteral("author_name"), data(index(row), AuthorNameRole));
    return v;
}

QVariantMap MessageListModel::lastOwnMessage() const
{
    if (!m_hooks.selfId)
        return {};
    const QString self = m_hooks.selfId();
    for (int i = 0; i < m_items.size(); ++i) {
        if (sid(m_items.at(i).json, "author_id") == self)
            return get(i);
    }
    return {};
}

QString MessageListModel::newestIdFrom(const QString& authorExcluded) const
{
    for (const auto& it : m_items) {
        if (sid(it.json, "author_id") != authorExcluded)
            return sid(it.json, "id");
    }
    return {};
}

bool MessageListModel::canFetchMore(const QModelIndex& parent) const
{
    return !parent.isValid() && !m_channel.isEmpty() && m_hasMore && !m_loading;
}

void MessageListModel::fetchMore(const QModelIndex& parent)
{
    if (!canFetchMore(parent) || !m_hooks.fetchPage)
        return;
    m_loading = true;
    emit loadingChanged();
    const QString before = m_items.isEmpty() ? QString() : idAt(static_cast<int>(m_items.size()) - 1);
    const auto gen = m_generation;
    m_hooks.fetchPage(m_channel, before, [this, gen](const QList<QJsonObject>& page, bool more, const QString& error) {
        if (gen != m_generation)
            return; // channel changed meanwhile
        m_loading = false;
        m_error = error;
        m_hasMore = error.isEmpty() && more;
        if (!page.isEmpty()) {
            const int first = static_cast<int>(m_items.size());
            beginInsertRows({}, first, first + static_cast<int>(page.size()) - 1);
            for (const auto& m : page)
                m_items.append(Item{
                    m, m_hooks.render ? m_hooks.render(m.value(QStringLiteral("content")).toString()) : QString()});
            endInsertRows();
            // The previously oldest row may now be grouped differently.
            if (first > 0)
                touchNeighbours(first - 1);
            emit countChanged();
        }
        emit loadingChanged();
    });
}

void MessageListModel::setChannel(const QString& channelId)
{
    if (channelId == m_channel)
        return;
    m_channel = channelId;
    reload();
}

void MessageListModel::reload()
{
    ++m_generation;
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();
    m_error.clear();
    m_loading = false;
    m_hasMore = !m_channel.isEmpty();
    emit loadingChanged();
    if (m_hasMore)
        fetchMore({});
}

void MessageListModel::touchNeighbours(int row)
{
    for (int r = std::max(0, row - 1); r <= std::min(static_cast<int>(m_items.size()) - 1, row + 1); ++r) {
        const auto idx = index(r);
        emit dataChanged(idx, idx, {GroupStartRole, DayStartRole, ReplyPreviewRole});
    }
}

void MessageListModel::addMessage(const QJsonObject& m)
{
    if (sid(m, "channel_id") != m_channel || rowOf(sid(m, "id")) >= 0)
        return;
    beginInsertRows({}, 0, 0);
    m_items.prepend(
        Item{m, m_hooks.render ? m_hooks.render(m.value(QStringLiteral("content")).toString()) : QString()});
    endInsertRows();
    touchNeighbours(1);
    emit countChanged();
}

void MessageListModel::updateMessage(const QJsonObject& m)
{
    const int row = rowOf(sid(m, "id"));
    if (row < 0)
        return;
    QJsonObject merged = m;
    // Update events do not carry the viewer's reaction flags; keep ours.
    merged.insert(QStringLiteral("reactions"), m_items[row].json.value(QStringLiteral("reactions")));
    m_items[row]
        = Item{merged, m_hooks.render ? m_hooks.render(m.value(QStringLiteral("content")).toString()) : QString()};
    emit dataChanged(index(row), index(row));
    // Replies quoting this message show a new preview.
    for (int i = 0; i < m_items.size(); ++i) {
        if (sid(m_items.at(i).json, "reply_to") == sid(m, "id"))
            emit dataChanged(index(i), index(i), {ReplyPreviewRole});
    }
}

void MessageListModel::removeMessage(const QString& id)
{
    const int row = rowOf(id);
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    m_items.removeAt(row);
    endRemoveRows();
    touchNeighbours(row);
    emit countChanged();
}

void MessageListModel::applyReaction(const QString& messageId, const QString& emoji, bool add, bool mine)
{
    const int row = rowOf(messageId);
    if (row < 0)
        return;
    QJsonArray reactions = m_items[row].json.value(QStringLiteral("reactions")).toArray();
    bool found = false;
    for (int i = 0; i < reactions.size(); ++i) {
        QJsonObject r = reactions.at(i).toObject();
        if (r.value(QStringLiteral("emoji")).toString() != emoji)
            continue;
        found = true;
        const int count = r.value(QStringLiteral("count")).toInt() + (add ? 1 : -1);
        if (count <= 0) {
            reactions.removeAt(i);
        } else {
            r.insert(QStringLiteral("count"), count);
            if (mine)
                r.insert(QStringLiteral("me"), add);
            reactions.replace(i, r);
        }
        break;
    }
    if (!found && add)
        reactions.append(QJsonObject{{"emoji", emoji}, {"count", 1}, {"me", mine}});
    m_items[row].json.insert(QStringLiteral("reactions"), reactions);
    emit dataChanged(index(row), index(row), {ReactionsRole});
}

void MessageListModel::refreshRendering()
{
    for (auto& it : m_items)
        it.html = m_hooks.render ? m_hooks.render(it.json.value(QStringLiteral("content")).toString()) : QString();
    if (!m_items.isEmpty())
        emit dataChanged(index(0), index(static_cast<int>(m_items.size()) - 1));
}

} // namespace omachat::client
