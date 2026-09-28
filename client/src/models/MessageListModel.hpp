#pragma once

#include <QAbstractListModel>
#include <QJsonObject>
#include <QList>

#include <functional>

namespace omachat::client {

// Messages of the selected channel, newest first (row 0 = newest), meant for
// a ListView with verticalLayoutDirection: ListView.BottomToTop. Older pages
// load through Qt's fetchMore protocol when the view reaches the top, so only
// visible delegates ever exist regardless of history length.
class MessageListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY loadingChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        AuthorIdRole,
        AuthorNameRole,
        AuthorColorRole,
        ContentRole,
        HtmlRole,
        TimestampRole,
        TimeTextRole,
        DayTextRole,
        EditedRole,
        IsActionRole,
        ReplyToRole,
        ReplyPreviewRole,
        MentionsMeRole,
        ReactionsRole,
        GroupStartRole,
        DayStartRole,
        IsOwnRole,
        AttachmentsRole,
    };

    struct Hooks {
        std::function<QString(const QString& userId)> displayName;
        std::function<QString(const QString& userId)> nameColor;
        std::function<QString(const QString& content)> render;
        std::function<QString()> selfId;
        // Requests a page; `done(messages newest-first, hasMore, error)`.
        std::function<void(const QString& channelId, const QString& beforeId,
            std::function<void(const QList<QJsonObject>&, bool, const QString&)> done)>
            fetchPage;
    };

    explicit MessageListModel(QObject* parent = nullptr);
    void setHooks(Hooks h) { m_hooks = std::move(h); }

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;

    void setChannel(const QString& channelId);
    QString channelId() const { return m_channel; }
    void reload();

    void addMessage(const QJsonObject& m); // new message pushed by the daemon
    void updateMessage(const QJsonObject& m);
    void removeMessage(const QString& id);
    void applyReaction(const QString& messageId, const QString& emoji, bool add, bool mine);
    void refreshRendering(); // names/theme changed

    bool loading() const { return m_loading; }
    bool hasMore() const { return m_hasMore; }
    QString error() const { return m_error; }
    int count() const { return static_cast<int>(m_items.size()); }

    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE int rowOf(const QString& id) const;
    // Newest message written by the current user (for Up-to-edit).
    Q_INVOKABLE QVariantMap lastOwnMessage() const;
    QString newestIdFrom(const QString& authorExcluded) const;

signals:
    void loadingChanged();
    void countChanged();

private:
    struct Item {
        QJsonObject json;
        QString html;
    };
    QString idAt(int row) const;
    void touchNeighbours(int row);

    Hooks m_hooks;
    QString m_channel;
    QList<Item> m_items;
    bool m_loading = false;
    bool m_hasMore = false;
    QString m_error;
    quint64 m_generation = 0;
};

} // namespace omachat::client
