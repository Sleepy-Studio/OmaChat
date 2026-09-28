#pragma once

#include "audio/AudioBackend.hpp"
#include "crypto/E2EManager.hpp"
#include "ipc/IpcServer.hpp"
#include "networking/FileTransfers.hpp"
#include "networking/ServerConnection.hpp"
#include "notifications/Notifier.hpp"
#include "omachat/config/ClientConfig.hpp"
#include "platform/CredentialStore.hpp"
#include "storage/LocalStore.hpp"
#include "video/VideoManager.hpp"
#include "voice/VoiceEngine.hpp"

#include <QHash>
#include <QObject>
#include <QTimer>

#include <functional>
#include <map>
#include <memory>
#include <set>

namespace omachat::daemon {

struct DaemonOptions {
    QString socketPath;
    QString databasePath;
    QString configPath;
    bool memoryCredentials = false;
    bool nullAudio = false;
    bool syntheticScreen = false; // share a test pattern instead of using the portal
    bool notifications = true;
};

// omachatd: owns the server sessions, voice engine and local IPC. The GUI,
// CLI and Omarchy plugin are all clients of this object via the socket.
//
// Every saved account keeps its own connection ("link"). One of them is
// active: IPC methods, events and status describe it, exactly as with a
// single account. The others stay connected in the background, notify and
// count unread messages, and become active with account.switch. Voice
// belongs to the active account; switching leaves it.
class Daemon : public QObject {
    Q_OBJECT
public:
    explicit Daemon(DaemonOptions options, QObject* parent = nullptr);
    ~Daemon() override;

    bool start(QString* error);
    void shutdown();

    // Exposed for in-process integration tests.
    ServerConnection& connection() { return *m_conn; }
    voice::VoiceEngine& voiceEngine() { return *m_voice; }
    audio::AudioBackend& audioBackend() { return *m_audio; }
    video::VideoManager& videoManager() { return *m_video; }
    QJsonObject statusJson() const;

private:
    using Method = std::function<void(const QJsonObject& params, const Responder& r)>;

    void registerMethods();
    void dispatch(const QString& method, const QJsonObject& params, const Responder& r);

    // helpers
    bool requireConnected(const Responder& r) const;
    void forward(
        proto::Envelope env, const Responder& r, std::function<QJsonObject(const proto::Envelope&)> transform = {});
    Id channelParam(const QJsonObject& params, const Responder& r, const char* key = "channel",
        ClientState::ChannelKind kind = ClientState::ChannelKind::Any);
    Id serverParam(const QJsonObject& params, const Responder& r, const char* key = "server");
    Id userParam(const QJsonObject& params, const Responder& r, const char* key = "user");

    // attachments
    void uploadAll(Id channelId, QStringList files, std::vector<proto::Attachment> done,
        std::function<void(
            bool ok, const QString& code, const QString& message, const std::vector<proto::Attachment>& attachments)>
            finish);
    void pruneAttachmentCache();
    QString downloadDestination(Id attachmentId, const QString& filename, const QString& to, QString* error) const;

    // voice
    void joinVoice(Id channelId, const Responder* r);
    void leaveVoice(const Responder* r);
    void stopVoiceEngine(); // video first, then voice
    void setSelfVoiceState(bool mute, bool deafen);
    void onVoiceSession(const proto::VoiceSession& session);
    void checkVoiceAfterSync();
    voice::VoiceEngine::Settings engineSettings() const;
    video::H264Encoder::Settings videoSettings() const;
    void setStreaming(bool streaming, std::function<void(bool ok, const QString& code, const QString& msg)> done);

    // connection / account
    struct Link {
        std::unique_ptr<ServerConnection> conn;
        std::unique_ptr<FileTransfers> transfers;
        std::unique_ptr<E2EManager> e2e;
        std::set<std::uint64_t> muted; // background notifications only
        int unread = 0; // messages while in the background
        int mentions = 0;
    };
    Link& linkFor(std::int64_t accountId); // created on first use
    void activate(std::int64_t accountId);
    void onBackgroundEvent(Link& link, const QString& name, const QJsonObject& data);
    QJsonArray accountsJson() const;
    void startAccount(const Account& account, ServerConnection::Credentials creds = {});
    void startLink(Link& link, const Account& account, ServerConnection::Credentials creds = {});
    // Sends through the active account; direct and group conversations are
    // end-to-end encrypted (files included) when the server supports it.
    void sendMessage(
        Id channelId, const QString& content, Id replyTo, bool action, const QStringList& files, const Responder& r);
    void onModelEvent(const QString& name, const QJsonObject& data);
    void maybeNotify(const ServerConnection& conn, const std::set<std::uint64_t>& muted, const QJsonObject& message);
    void scheduleStatus();

    void applyConfig();
    bool saveConfig(QString* error);

    DaemonOptions m_options;
    config::ClientConfig m_config;
    LocalStore m_store;
    std::unique_ptr<ICredentialStore> m_credentials;
    std::map<std::int64_t, Link> m_links; // by account id; 0 = no account yet
    ServerConnection* m_conn = nullptr; // the active link
    FileTransfers* m_transfers = nullptr;
    E2EManager* m_e2e = nullptr;
    std::int64_t m_active = 0;
    std::unique_ptr<audio::AudioBackend> m_audio;
    std::unique_ptr<voice::VoiceEngine> m_voice;
    std::unique_ptr<video::VideoManager> m_video;
    IpcServer m_ipc;
    Notifier m_notifier;
    QHash<QString, Method> m_methods;

    // voice state owned by the daemon
    Id m_voiceChannel = 0; // channel we intend to be in
    bool m_voicePending = false;
    bool m_selfMute = false;
    bool m_selfDeaf = false;
    QLocalSocket* m_pttOwner = nullptr;
    bool m_streamConfirmed = false; // the server accepted our SetStreaming(true)
    std::set<Id> m_speaking;
    QString m_audioError;

    // UI hints for notification suppression
    Id m_focusedChannel = 0;
    Id m_focusedServer = 0;
    bool m_windowFocused = false;
    std::set<std::uint64_t> m_mutedChannels;

    QTimer m_statusTimer;
};

} // namespace omachat::daemon
