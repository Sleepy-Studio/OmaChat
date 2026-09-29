#pragma once

#include "application/DaemonLink.hpp"
#include "models/MessageListModel.hpp"
#include "models/RowListModel.hpp"
#include "omachat/config/ClientConfig.hpp"
#include "text/MarkdownRenderer.hpp"

#include <QHash>
#include <QJsonArray>
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
    Q_PROPERTY(QString accountId READ accountId NOTIFY statusChanged)
    Q_PROPERTY(QString instanceName READ instanceName NOTIFY statusChanged)
    Q_PROPERTY(bool authBusy READ authBusy NOTIFY authChanged)
    Q_PROPERTY(QString authError READ authError NOTIFY authChanged)
    // Provider sign-in methods linked to the current account (Settings > Account).
    Q_PROPERTY(QVariantList oauthIdentities READ oauthIdentities NOTIFY oauthIdentitiesChanged)
    Q_PROPERTY(bool oauthLinkBusy READ oauthLinkBusy NOTIFY oauthIdentitiesChanged)
    Q_PROPERTY(QString oauthLinkMessage READ oauthLinkMessage NOTIFY oauthIdentitiesChanged)
    Q_PROPERTY(bool oauthLinkError READ oauthLinkError NOTIFY oauthIdentitiesChanged)

    // self
    Q_PROPERTY(QString selfId READ selfId NOTIFY statusChanged)
    Q_PROPERTY(QString selfName READ selfName NOTIFY statusChanged)
    Q_PROPERTY(QString selfUsername READ selfUsername NOTIFY statusChanged)
    Q_PROPERTY(QString selfStatus READ selfStatus NOTIFY statusChanged)
    Q_PROPERTY(QString selfBio READ selfBio NOTIFY statusChanged)
    Q_PROPERTY(QString selfAvatarUrl READ selfAvatarUrl NOTIFY statusChanged)
    Q_PROPERTY(int profilesRevision READ profilesRevision NOTIFY profilesChanged)

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
    // screen sharing (StreamActions in AppActions.cpp)
    Q_PROPERTY(bool canShareScreen READ canShareScreen NOTIFY voiceChanged)
    Q_PROPERTY(bool sharingScreen READ sharingScreen NOTIFY voiceChanged)
    Q_PROPERTY(bool shareStarting READ shareStarting NOTIFY voiceChanged)
    // [{userId, name, path}] for every stream being watched, newest last
    Q_PROPERTY(QVariantList watchedStreams READ watchedStreams NOTIFY voiceChanged)
    // Frame file for the local user's own picture-in-picture preview while sharing.
    Q_PROPERTY(QString selfPreviewPath READ selfPreviewPath NOTIFY voiceChanged)

    // navigation
    Q_PROPERTY(QString selectedServerId READ selectedServerId NOTIFY selectionChanged)
    Q_PROPERTY(bool discordImportBusy READ discordImportBusy NOTIFY discordImportChanged)
    Q_PROPERTY(QString discordImportStatus READ discordImportStatus NOTIFY discordImportChanged)
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
    Q_PROPERTY(bool canManageRoles READ canManageRoles NOTIFY selectionChanged)
    // The selected conversation is end-to-end encrypted.
    Q_PROPERTY(bool selectedEncrypted READ selectedEncrypted NOTIFY selectionChanged)

    // roles of the selected server, highest first (RoleActions.cpp)
    Q_PROPERTY(QVariantList serverRoles READ serverRoles NOTIFY rolesChanged)
    Q_PROPERTY(QVariantList serverMembers READ serverMembers NOTIFY rolesChanged)
    Q_PROPERTY(QVariantList serverEmoji READ serverEmoji NOTIFY emojiListChanged)
    Q_PROPERTY(bool canManageEmoji READ canManageEmoji NOTIFY rolesChanged)
    Q_PROPERTY(QVariantList channelOverrides READ channelOverrides NOTIFY overridesChanged)
    Q_PROPERTY(QVariantList safetyNumbers READ safetyNumbers NOTIFY safetyChanged)
    Q_PROPERTY(QVariantList permissionCatalog READ permissionCatalog CONSTANT)

    // composer
    Q_PROPERTY(QString replyToId READ replyToId NOTIFY replyChanged)
    Q_PROPERTY(QString replyToPreview READ replyToPreview NOTIFY replyChanged)
    Q_PROPERTY(QString typingText READ typingText NOTIFY typingChanged)

    // transient feedback
    Q_PROPERTY(QVariantList pendingFiles READ pendingFiles NOTIFY attachmentsChanged)
    Q_PROPERTY(QVariantList uploads READ uploads NOTIFY attachmentsChanged)
    Q_PROPERTY(QVariantMap previews READ previews NOTIFY previewsChanged)
    Q_PROPERTY(bool attachmentsSupported READ attachmentsSupported NOTIFY statusChanged)
    // Every saved account (status.accounts): id, host, username, state, active, unread, mentions.
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY statusChanged)
    Q_PROPERTY(int backgroundUnread READ backgroundUnread NOTIFY statusChanged)
    // True while the login form is open for another account.
    Q_PROPERTY(bool addingAccount READ addingAccount WRITE setAddingAccount NOTIFY authChanged)
    // Features the connected server announced ("search.server", "dm.group", …).
    Q_PROPERTY(QStringList capabilities READ capabilities NOTIFY statusChanged)

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
    Q_PROPERTY(QVariantMap videoSettings READ videoSettings NOTIFY audioChanged)
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
    QString accountId() const { return account().value(QStringLiteral("id")).toString(); }
    QString instanceName() const { return m_status.value(QStringLiteral("instance")).toString(); }
    bool authBusy() const { return m_authBusy; }
    QString authError() const { return m_authError; }
    QVariantList oauthIdentities() const { return m_oauthIdentities; }
    bool oauthLinkBusy() const { return m_oauthLinkBusy; }
    QString oauthLinkMessage() const { return m_oauthLinkMessage; }
    bool oauthLinkError() const { return m_oauthLinkError; }
    QString selfId() const { return m_self.value(QStringLiteral("id")).toString(); }
    QString selfName() const { return m_self.value(QStringLiteral("display_name")).toString(); }
    QString selfUsername() const { return m_self.value(QStringLiteral("username")).toString(); }
    QString selfStatus() const { return m_self.value(QStringLiteral("status")).toString(QStringLiteral("offline")); }
    QString selfBio() const { return m_self.value(QStringLiteral("bio")).toString(); }
    QString selfAvatarUrl() const { return m_self.value(QStringLiteral("avatar_url")).toString(); }
    int profilesRevision() const { return m_profilesRevision; }

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
    bool canShareScreen() const;
    bool sharingScreen() const { return voice().value(QStringLiteral("streaming")).toBool(); }
    bool shareStarting() const { return m_shareStarting; }
    QVariantList watchedStreams() const;
    QString selfPreviewPath() const { return voice().value(QStringLiteral("self_preview")).toString(); }
    QString audioError() const
    {
        return m_status.value(QStringLiteral("audio")).toObject().value(QStringLiteral("error")).toString();
    }

    QString selectedServerId() const { return m_selectedServer; }
    bool discordImportBusy() const { return m_discordImportBusy; }
    QString discordImportStatus() const { return m_discordImportStatus; }
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
    bool canManageRoles() const;
    bool selectedEncrypted() const;
    QVariantList serverRoles() const;
    QVariantList serverMembers() const;
    QVariantList channelOverrides() const { return m_channelOverrides; }
    QVariantList safetyNumbers() const { return m_safetyNumbers; }
    QVariantList permissionCatalog() const;

    QString replyToId() const { return m_replyTo; }
    QString replyToPreview() const { return m_replyPreview; }
    QString typingText() const { return m_typingText; }
    QVariantList pendingFiles() const { return m_pendingFiles; }
    QVariantList uploads() const;
    QVariantMap previews() const { return m_previews; }
    bool attachmentsSupported() const { return maxUploadBytes() > 0; }
    QVariantList accounts() const { return m_status.value(QStringLiteral("accounts")).toArray().toVariantList(); }
    int backgroundUnread() const
    {
        int n = 0;
        for (const auto& a : m_status.value(QStringLiteral("accounts")).toArray())
            n += a.toObject().value(QStringLiteral("active")).toBool()
                ? 0
                : a.toObject().value(QStringLiteral("mentions")).toInt();
        return n;
    }
    bool addingAccount() const { return m_addingAccount; }
    void setAddingAccount(bool adding)
    {
        if (adding == m_addingAccount)
            return;
        m_addingAccount = adding;
        m_authError.clear();
        emit authChanged();
    }
    QStringList capabilities() const
    {
        QStringList out;
        for (const auto& v : m_status.value(QStringLiteral("capabilities")).toArray())
            out << v.toString();
        return out;
    }
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
    QVariantMap videoSettings() const { return m_videoSettings; }
    QVariantMap notificationSettings() const;
    QString version() const;

    // ---- actions (AppActions.cpp)
    Q_INVOKABLE void retryDaemon();
    Q_INVOKABLE void login(const QString& host, int port, const QString& username, const QString& password,
        bool registerAccount, const QString& displayName);
    // provider is "discord", "github" or "google". Opens the system browser;
    // the daemon runs the whole authorization-code exchange and reports back
    // through the usual status/authError properties.
    Q_INVOKABLE void loginWithOAuth(const QString& host, int port, const QString& provider);
    // Attaches/detaches a provider from the already-signed-in account.
    Q_INVOKABLE void refreshOAuthIdentities();
    Q_INVOKABLE void linkOAuthProvider(const QString& provider);
    Q_INVOKABLE void unlinkOAuthProvider(const QString& provider);
    Q_INVOKABLE void trustCertificate();
    Q_INVOKABLE void reconnect();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void switchAccount(const QString& accountId);
    Q_INVOKABLE void removeAccount(const QString& accountId);

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
    // wholeServer searches every readable channel of the selected server.
    Q_INVOKABLE void search(const QString& query, bool wholeServer = false);
    Q_INVOKABLE void clearSearch();

    Q_INVOKABLE void joinVoice(const QString& channelId);
    Q_INVOKABLE void leaveVoice();
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void toggleDeafen();
    Q_INVOKABLE void pushToTalk(bool pressed);
    Q_INVOKABLE void setInputMode(const QString& mode);
    Q_INVOKABLE void setUserVolume(const QString& userId, int percent);
    Q_INVOKABLE void toggleScreenShare();
    Q_INVOKABLE void startScreenShare(bool audio);
    Q_INVOKABLE void watchStream(const QString& userId);
    Q_INVOKABLE void unwatchStream(const QString& userId);
    // Ephemeral laser pointer while sharing your own screen; x/y normalized
    // 0..1 over the captured frame. Fire-and-forget, no notice on failure.
    Q_INVOKABLE void sendPointer(bool active, double x, double y);
    Q_INVOKABLE bool isStreaming(const QString& userId) const;
    Q_INVOKABLE int userVolume(const QString& userId) const;

    Q_INVOKABLE void createServer(const QString& name);
    Q_INVOKABLE void createServerFromDiscord(const QString& name, const QVariantList& files);
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
    // Group conversations (AppActions.cpp)
    Q_INVOKABLE QVariantList knownUsers() const; // everyone you share a server with
    Q_INVOKABLE QStringList channelRecipients(const QString& channelId) const;
    // Safety numbers (E2E): {userId, name, number, devices, verified} per other participant.
    Q_INVOKABLE void loadSafetyNumbers();
    Q_INVOKABLE void setVerified(const QString& userId, bool verified);
    Q_INVOKABLE void createGroup(const QStringList& userIds, const QString& name);
    Q_INVOKABLE void addToGroup(const QString& channelId, const QStringList& userIds);
    Q_INVOKABLE void renameGroup(const QString& channelId, const QString& name);
    Q_INVOKABLE void leaveGroup(const QString& channelId);
    Q_INVOKABLE void kick(const QString& userId, const QString& reason);
    Q_INVOKABLE void ban(const QString& userId, const QString& reason);
    Q_INVOKABLE void setPresence(const QString& status);
    Q_INVOKABLE void updateProfile(const QString& displayName, const QString& avatarUrl, const QString& bio);
    Q_INVOKABLE QVariantMap userProfile(const QString& userId) const;

    Q_INVOKABLE void switcherQuery(const QString& text);
    Q_INVOKABLE void switcherActivate(int row);

    Q_INVOKABLE void refreshAudio();
    Q_INVOKABLE void setAudio(const QString& key, const QVariant& value);
    Q_INVOKABLE void setVideo(const QString& key, const QVariant& value);
    Q_INVOKABLE void setNotification(const QString& key, bool enabled);
    Q_INVOKABLE void setWindowFocused(bool focused);
    Q_INVOKABLE void dismissNotice();

    // ---- attachments (AttachmentActions.cpp)
    Q_INVOKABLE void addFiles(const QVariantList& urls);
    // Attaches an image or copied files from the clipboard. Returns false when
    // the clipboard holds neither, so the composer pastes text as usual.
    Q_INVOKABLE bool pasteAttachment();
    Q_INVOKABLE void removePendingFile(int index);
    Q_INVOKABLE void requestPreview(const QString& attachmentId, const QString& filename, double size);
    Q_INVOKABLE void requestMedia(const QString& attachmentId, const QString& filename);
    Q_INVOKABLE void saveAttachment(const QString& attachmentId, const QString& filename);
    Q_INVOKABLE void openAttachment(const QString& attachmentId, const QString& filename, double size);
    Q_INVOKABLE void cancelTransfer(const QString& transferId);
    Q_INVOKABLE QString formatSize(double bytes) const;
    Q_INVOKABLE QString formatDuration(qint64 ms) const;

    // emoji
    Q_INVOKABLE QVariantList emojiCatalog() const;
    Q_INVOKABLE QVariantList recentEmoji() const;
    Q_INVOKABLE void noteEmojiUsed(const QString& glyph);
    QVariantList serverEmoji() const;
    bool canManageEmoji() const;
    Q_INVOKABLE void createServerEmoji(const QString& name, const QUrl& fileUrl);
    Q_INVOKABLE void deleteServerEmoji(const QString& emojiId);
    Q_INVOKABLE QVariantMap customEmojiByName(const QString& name) const; // {} if unknown on this server

    // ---- roles and channel permissions (RoleActions.cpp)
    Q_INVOKABLE void createRole(const QString& name);
    Q_INVOKABLE void updateRole(
        const QString& roleId, const QString& name, const QString& color, const QStringList& permissions);
    Q_INVOKABLE void moveRole(const QString& roleId, int delta);
    Q_INVOKABLE void deleteRole(const QString& roleId);
    Q_INVOKABLE void setMemberRole(const QString& userId, const QString& roleId, bool add);
    Q_INVOKABLE void loadOverrides(const QString& channelId);
    // targetType is "role" or "user"; empty allow and deny remove the override.
    Q_INVOKABLE void setOverride(const QString& channelId, const QString& targetType, const QString& targetId,
        const QStringList& allow, const QStringList& deny);

    Q_INVOKABLE QString userName(const QString& userId) const;
    Q_INVOKABLE QString userColor(const QString& userId) const;
    Q_INVOKABLE QString userStatus(const QString& userId) const;

signals:
    void daemonChanged();
    void statusChanged();
    void profilesChanged();
    void authChanged();
    void oauthIdentitiesChanged();
    void voiceChanged();
    void requestScreenAudioConsent();
    void selectionChanged();
    void discordImportChanged();
    void discordImportFinished(bool success);
    void replyChanged();
    void typingChanged();
    void noticeChanged();
    void configChanged();
    void audioChanged();
    void attachmentsChanged();
    void previewsChanged();
    void rolesChanged();
    void emojiListChanged();
    void safetyChanged();
    void overridesChanged();
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
    void sendMessage(const QString& channelId, const QString& content, const QString& replyTo, bool action,
        const QString& original, const QVariantList& files = {});
    double maxUploadBytes() const { return m_status.value(QStringLiteral("max_upload_bytes")).toDouble(); }
    void onTransferProgress(const QJsonObject& data);
    static void prunePastedImages();
    int selfRank() const; // highest role position held in the selected server
    void persistSelection();

    config::ClientConfig m_config;
    bool m_discordImportBusy = false;
    QString m_discordImportStatus;
    DaemonLink m_link;
    MarkdownRenderer m_markdown;

    QJsonObject m_status;
    QJsonObject m_self;
    int m_profilesRevision = 0;
    QHash<QString, QJsonObject> m_serversById;
    QHash<QString, QJsonObject> m_channelsById;
    QHash<QString, QJsonObject> m_usersById;
    QHash<QString, QJsonObject> m_rolesById;
    QHash<QString, QJsonObject> m_emojiById;
    QVariantList m_channelOverrides;
    QVariantList m_safetyNumbers;
    bool m_addingAccount = false;
    bool m_shareStarting = false;
    QString m_overridesChannel;
    QHash<QString, QHash<QString, QJsonObject>> m_membersByServer;
    QHash<QString, QJsonObject> m_voiceByUser;
    QSet<QString> m_speaking;
    QHash<QString, int> m_unread;
    QHash<QString, int> m_mentions;
    QSet<QString> m_mutedChannels;
    mutable QVariantList m_emojiCatalog; // lazily loaded from the bundled resource, then cached
    QStringList m_recentEmoji; // most-recently-used first, session-local only
    QSet<QString> m_collapsed;
    QHash<QString, double> m_userVolumes;
    QHash<QString, QString> m_lastChannelForServer;

    QString m_selectedServer = QStringLiteral("home");
    QString m_selectedChannel;
    QString m_replyTo;
    QString m_replyPreview;
    bool m_authBusy = false;
    QString m_authError;
    QVariantList m_oauthIdentities;
    bool m_oauthLinkBusy = false;
    QString m_oauthLinkMessage;
    bool m_oauthLinkError = false;
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

    QVariantList m_pendingFiles; // {path, name, size} waiting in the composer
    QHash<QString, QJsonObject> m_uploads; // transfer id -> progress
    QVariantMap m_previews; // attachment id -> local file URL
    QSet<QString> m_previewRequests;

    QVariantList m_inputDevices;
    QVariantList m_outputDevices;
    QVariantMap m_videoSettings;
    QVariantMap m_audioSettings;

    RowListModel m_servers;
    RowListModel m_channels;
    RowListModel m_members;
    RowListModel m_switcher;
    RowListModel m_searchResults;
    MessageListModel m_messages;
};

} // namespace omachat::client
