#pragma once

#include "application/DaemonLink.hpp"
#include "models/MessageListModel.hpp"
#include "models/RowListModel.hpp"
#include "omachat/config/ClientConfig.hpp"
#include "text/MarkdownRenderer.hpp"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QQmlEngine>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace omachat::client {

// The single QML-facing controller ("App" in QML). It mirrors daemon state
// into properties and list models and turns user intent into IPC requests.
// It never touches the network, credentials or audio directly: all of that
// lives in omachatd. Passwords typed in the login form pass straight through
// to the daemon and are not retained here.
class AppController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(App)
    QML_SINGLETON

    // daemon link
    Q_PROPERTY(QString daemonState READ daemonState NOTIFY daemonChanged)
    Q_PROPERTY(QString daemonError READ daemonError NOTIFY daemonChanged)

    // server connection (from omachatd)
    Q_PROPERTY(QString state READ state NOTIFY statusChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY statusChanged)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY statusChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY statusChanged)
    Q_PROPERTY(QString certificateFingerprint READ certificateFingerprint NOTIFY statusChanged)
    Q_PROPERTY(int reconnectInMs READ reconnectInMs NOTIFY statusChanged)
    Q_PROPERTY(QString accountHost READ accountHost NOTIFY statusChanged)
    Q_PROPERTY(int accountPort READ accountPort NOTIFY statusChanged)
    Q_PROPERTY(QString accountUser READ accountUser NOTIFY statusChanged)
    Q_PROPERTY(QString instanceName READ instanceName NOTIFY statusChanged)
    Q_PROPERTY(bool authBusy READ authBusy NOTIFY authChanged)
    Q_PROPERTY(QString authError READ authError NOTIFY authChanged)

    // self
    Q_PROPERTY(QString selfId READ selfId NOTIFY statusChanged)
    Q_PROPERTY(QString selfName READ selfName NOTIFY statusChanged)
    Q_PROPERTY(QString selfUsername READ selfUsername NOTIFY statusChanged)
    Q_PROPERTY(QString selfStatus READ selfStatus NOTIFY statusChanged)

    // voice
    Q_PROPERTY(bool voiceJoined READ voiceJoined NOTIFY voiceChanged)
    Q_PROPERTY(bool voicePending READ voicePending NOTIFY voiceChanged)
    Q_PROPERTY(QString voiceChannelId READ voiceChannelId NOTIFY voiceChanged)
    Q_PROPERTY(QString voiceChannelName READ voiceChannelName NOTIFY voiceChanged)
    Q_PROPERTY(QString voiceServerName READ voiceServerName NOTIFY voiceChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY voiceChanged)
    Q_PROPERTY(bool deafened READ deafened NOTIFY voiceChanged)
    Q_PROPERTY(QString inputMode READ inputMode NOTIFY voiceChanged)
    Q_PROPERTY(bool pttActive READ pttActive NOTIFY voiceChanged)
    Q_PROPERTY(bool transmitting READ transmitting NOTIFY voiceChanged)
    Q_PROPERTY(bool voiceConnected READ voiceConnected NOTIFY voiceChanged)
    Q_PROPERTY(QString audioError READ audioError NOTIFY voiceChanged)

    // navigation
    Q_PROPERTY(QString selectedServerId READ selectedServerId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedServerName READ selectedServerName NOTIFY selectionChanged)
    Q_PROPERTY(bool homeSelected READ homeSelected NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedChannelId READ selectedChannelId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedChannelName READ selectedChannelName NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedChannelTopic READ selectedChannelTopic NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedChannelType READ selectedChannelType NOTIFY selectionChanged)
    Q_PROPERTY(bool canSend READ canSend NOTIFY selectionChanged)
    Q_PROPERTY(bool canManageMessages READ canManageMessages NOTIFY selectionChanged)
    Q_PROPERTY(bool canManageChannels READ canManageChannels NOTIFY selectionChanged)
    Q_PROPERTY(bool canCreateInvites READ canCreateInvites NOTIFY selectionChanged)
    Q_PROPERTY(bool canKick READ canKick NOTIFY selectionChanged)
    Q_PROPERTY(bool canBan READ canBan NOTIFY selectionChanged)
    Q_PROPERTY(bool isServerOwner READ isServerOwner NOTIFY selectionChanged)

    // composer
    Q_PROPERTY(QString replyToId READ replyToId NOTIFY replyChanged)
    Q_PROPERTY(QString replyToPreview READ replyToPreview NOTIFY replyChanged)
    Q_PROPERTY(QString typingText READ typingText NOTIFY typingChanged)

    // transient feedback
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(bool noticeIsError READ noticeIsError NOTIFY noticeChanged)

    // models
    Q_PROPERTY(omachat::client::RowListModel* servers READ servers CONSTANT)
    Q_PROPERTY(omachat::client::RowListModel* channels READ channels CONSTANT)
    Q_PROPERTY(omachat::client::RowListModel* members READ members CONSTANT)
    Q_PROPERTY(omachat::client::RowListModel* switcher READ switcher CONSTANT)
    Q_PROPERTY(omachat::client::RowListModel* searchResults READ searchResults CONSTANT)
    Q_PROPERTY(omachat::client::MessageListModel* messages READ messages CONSTANT)

    // settings
    Q_PROPERTY(QVariantMap shortcuts READ shortcuts NOTIFY configChanged)
    Q_PROPERTY(bool compactMode READ compactMode NOTIFY configChanged)
    Q_PROPERTY(int pushToTalkKey READ pushToTalkKey NOTIFY configChanged)
    Q_PROPERTY(QVariantList commandHelp READ commandHelp CONSTANT)
    Q_PROPERTY(QVariantList inputDevices READ inputDevices NOTIFY audioChanged)
    Q_PROPERTY(QVariantList outputDevices READ outputDevices NOTIFY audioChanged)
    Q_PROPERTY(QVariantMap audioSettings READ audioSettings NOTIFY audioChanged)
    Q_PROPERTY(QVariantMap notificationSettings READ notificationSettings NOTIFY configChanged)
    Q_PROPERTY(QString version READ version CONSTANT)

public:
    explicit AppController(const config::ClientConfig& config, QObject* parent = nullptr);

    static AppController* instance();
    static AppController* create(QQmlEngine*, QJSEngine*);

    void start();

    // ---- property getters
    QString daemonState() const { return m_link.stateName(); }
    QString daemonError() const { return m_link.error(); }
    QString state() const { return m_status.value(QStringLiteral("state")).toString(QStringLiteral("starting")); }
    bool ready() const;
    QString errorCode() const;
    QString errorMessage() const;
    QString certificateFingerprint() const;
    int reconnectInMs() const { return m_status.value(QStringLiteral("reconnect_in_ms")).toInt(); }
    QString accountHost() const { return account().value(QStringLiteral("host")).toString(); }
    int accountPort() const { return account().value(QStringLiteral("port")).toInt(6473); }
    QString accountUser() const { return account().value(QStringLiteral("username")).toString(); }
    QString instanceName() const { return m_status.value(QStringLiteral("instance")).toString(); }
    bool authBusy() const { return m_authBusy; }
    QString authError() const { return m_authError; }
    QString selfId() const { return m_self.value(QStringLiteral("id")).toString(); }
    QString selfName() const { return m_self.value(QStringLiteral("display_name")).toString(); }
    QString selfUsername() const { return m_self.value(QStringLiteral("username")).toString(); }
    QString selfStatus() const { return m_self.value(QStringLiteral("status")).toString(QStringLiteral("offline")); }

    bool voiceJoined() const { return voice().value(QStringLiteral("joined")).toBool(); }
    bool voicePending() const { return voice().value(QStringLiteral("pending")).toBool(); }
    QString voiceChannelId() const { return voice().value(QStringLiteral("channel_id")).toString(); }
    QString voiceChannelName() const { return voice().value(QStringLiteral("channel")).toString(); }
    QString voiceServerName() const;
    bool muted() const { return voice().value(QStringLiteral("muted")).toBool(); }
    bool deafened() const { return voice().value(QStringLiteral("deafened")).toBool(); }
    QString inputMode() const { return voice().value(QStringLiteral("mode")).toString(QStringLiteral("vad")); }
    bool pttActive() const { return voice().value(QStringLiteral("ptt")).toBool(); }
    bool transmitting() const { return m_speaking.contains(selfId()); }
    bool voiceConnected() const { return voice().value(QStringLiteral("registered")).toBool(); }
    QString audioError() const
    {
        return m_status.value(QStringLiteral("audio")).toObject().value(QStringLiteral("error")).toString();
    }

    QString selectedServerId() const { return m_selectedServer; }
    QString selectedServerName() const;
    bool homeSelected() const { return m_selectedServer == u"home"; }
    QString selectedChannelId() const { return m_selectedChannel; }
    QString selectedChannelName() const;
    QString selectedChannelTopic() const;
    QString selectedChannelType() const;
    bool canSend() const;
    bool canManageMessages() const;
    bool canManageChannels() const;
    bool canCreateInvites() const;
    bool canKick() const;
    bool canBan() const;
    bool isServerOwner() const;

    QString replyToId() const { return m_replyTo; }
    QString replyToPreview() const { return m_replyPreview; }
    QString typingText() const { return m_typingText; }
    QString notice() const { return m_notice; }
    bool noticeIsError() const { return m_noticeError; }

    RowListModel* servers() { return &m_servers; }
    RowListModel* channels() { return &m_channels; }
    RowListModel* members() { return &m_members; }
    RowListModel* switcher() { return &m_switcher; }
    RowListModel* searchResults() { return &m_searchResults; }
    MessageListModel* messages() { return &m_messages; }

    QVariantMap shortcuts() const;
    bool compactMode() const { return m_config.ui.compactMode; }
    int pushToTalkKey() const;
    QVariantList commandHelp() const;
    QVariantList inputDevices() const { return m_inputDevices; }
    QVariantList outputDevices() const { return m_outputDevices; }
    QVariantMap audioSettings() const { return m_audioSettings; }
    QVariantMap notificationSettings() const;
    QString version() const;

    // ---- actions (AppActions.cpp)
    Q_INVOKABLE void retryDaemon();
    Q_INVOKABLE void login(const QString& host, int port, const QString& username, const QString& password,
        bool registerAccount, const QString& displayName);
    Q_INVOKABLE void trustCertificate();
    Q_INVOKABLE void reconnect();
    Q_INVOKABLE void logout();

    Q_INVOKABLE void selectHome();
    Q_INVOKABLE void selectServer(const QString& id);
    Q_INVOKABLE void selectChannel(const QString& id);
    Q_INVOKABLE void selectRelativeChannel(int delta);
    Q_INVOKABLE void toggleCategory(const QString& id);

    Q_INVOKABLE bool sendComposer(const QString& text);
    Q_INVOKABLE void editMessage(const QString& id, const QString& text);
    Q_INVOKABLE void deleteMessage(const QString& id);
    Q_INVOKABLE void toggleReaction(const QString& messageId, const QString& emoji);
    Q_INVOKABLE void startReply(const QString& messageId);
    Q_INVOKABLE void cancelReply();
    Q_INVOKABLE void notifyTyping();
    Q_INVOKABLE QVariantMap complete(const QString& textBeforeCursor) const;
    Q_INVOKABLE void copyText(const QString& text);
    Q_INVOKABLE void openLink(const QString& url);
    Q_INVOKABLE void search(const QString& query);
    Q_INVOKABLE void clearSearch();

    Q_INVOKABLE void joinVoice(const QString& channelId);
    Q_INVOKABLE void leaveVoice();
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void toggleDeafen();
    Q_INVOKABLE void pushToTalk(bool pressed);
    Q_INVOKABLE void setInputMode(const QString& mode);
    Q_INVOKABLE void setUserVolume(const QString& userId, int percent);
    Q_INVOKABLE int userVolume(const QString& userId) const;

    Q_INVOKABLE void createServer(const QString& name);
    Q_INVOKABLE void joinServer(const QString& invite);
    Q_INVOKABLE void createInvite();
    Q_INVOKABLE void leaveServer(const QString& id);
    Q_INVOKABLE void deleteServer(const QString& id);
    Q_INVOKABLE void createChannel(const QString& name, const QString& type, const QString& parentId);
    Q_INVOKABLE void deleteChannel(const QString& id);
    Q_INVOKABLE void setTopic(const QString& topic);
    Q_INVOKABLE void setChannelMuted(const QString& id, bool muted);
    Q_INVOKABLE bool channelMuted(const QString& id) const { return m_mutedChannels.contains(id); }
    Q_INVOKABLE void openDm(const QString& userId);
    Q_INVOKABLE void kick(const QString& userId, const QString& reason);
    Q_INVOKABLE void ban(const QString& userId, const QString& reason);
    Q_INVOKABLE void setPresence(const QString& status);

    Q_INVOKABLE void switcherQuery(const QString& text);
    Q_INVOKABLE void switcherActivate(int row);

    Q_INVOKABLE void refreshAudio();
    Q_INVOKABLE void setAudio(const QString& key, const QVariant& value);
    Q_INVOKABLE void setNotification(const QString& key, bool enabled);
    Q_INVOKABLE void setWindowFocused(bool focused);
    Q_INVOKABLE void dismissNotice();
    Q_INVOKABLE void showAttachmentNotice();

    Q_INVOKABLE QString userName(const QString& userId) const;
    Q_INVOKABLE QString userColor(const QString& userId) const;
    Q_INVOKABLE QString userStatus(const QString& userId) const;

signals:
    void daemonChanged();
    void statusChanged();
    void authChanged();
    void voiceChanged();
    void selectionChanged();
    void replyChanged();
    void typingChanged();
    void noticeChanged();
    void configChanged();
    void audioChanged();
    // QML hooks
    void composerRestore(const QString& text);
    void focusComposer();
    void requestInviteJoin(const QString& invite);

private:
    // AppController.cpp
    void onDaemonConnected();
    void onEvent(const QString& name, const QJsonObject& data);
    void applyStatus(const QJsonObject& status);
    void loadSnapshot();
    void applySnapshot(const QJsonObject& snap);
    void rebuildServers();
    void rebuildChannels();
    void rebuildMembers();
    void ensureSelection();
    void markRead(const QString& channelId);
    void updateTyping();
    void showNotice(const QString& text, bool error = false);
    void call(const QString& method, const QJsonObject& params = {}, std::function<void(const QJsonObject&)> onOk = {},
        const QString& failurePrefix = {});
    QJsonObject account() const { return m_status.value(QStringLiteral("account")).toObject(); }
    QJsonObject voice() const { return m_status.value(QStringLiteral("voice")).toObject(); }
    QJsonObject channel(const QString& id) const { return m_channelsById.value(id); }
    bool channelFlag(const QString& flag) const;
    bool serverPermission(const char* name) const;
    QString roleColorFor(const QString& serverId, const QString& userId) const;
    QString renderMarkdown(const QString& content) const;
    void runCommand(const struct Command& cmd, const QString& original);
    void sendMessage(
        const QString& channelId, const QString& content, const QString& replyTo, bool action, const QString& original);
    void persistSelection();

    config::ClientConfig m_config;
    DaemonLink m_link;
    MarkdownRenderer m_markdown;

    QJsonObject m_status;
    QJsonObject m_self;
    QHash<QString, QJsonObject> m_serversById;
    QHash<QString, QJsonObject> m_channelsById;
    QHash<QString, QJsonObject> m_usersById;
    QHash<QString, QJsonObject> m_rolesById;
    QHash<QString, QHash<QString, QJsonObject>> m_membersByServer;
    QHash<QString, QJsonObject> m_voiceByUser;
    QSet<QString> m_speaking;
    QHash<QString, int> m_unread;
    QHash<QString, int> m_mentions;
    QSet<QString> m_mutedChannels;
    QSet<QString> m_collapsed;
    QHash<QString, double> m_userVolumes;
    QHash<QString, QString> m_lastChannelForServer;

    QString m_selectedServer = QStringLiteral("home");
    QString m_selectedChannel;
    QString m_replyTo;
    QString m_replyPreview;
    bool m_authBusy = false;
    QString m_authError;
    QString m_notice;
    bool m_noticeError = false;
    QTimer m_noticeTimer;
    bool m_windowFocused = true;
    bool m_initialSelectionDone = false;

    QHash<QString, QHash<QString, qint64>> m_typing; // channel -> user -> expiry
    QString m_typingText;
    QTimer m_typingTimer;
    qint64 m_lastTypingSent = 0;
    QString m_lastTypingChannel;

    QVariantList m_inputDevices;
    QVariantList m_outputDevices;
    QVariantMap m_audioSettings;

    RowListModel m_servers;
    RowListModel m_channels;
    RowListModel m_members;
    RowListModel m_switcher;
    RowListModel m_searchResults;
    MessageListModel m_messages;
};

} // namespace omachat::client
