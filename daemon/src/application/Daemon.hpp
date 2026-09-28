#pragma once

#include "audio/AudioBackend.hpp"
#include "ipc/IpcServer.hpp"
#include "networking/ServerConnection.hpp"
#include "notifications/Notifier.hpp"
#include "omachat/config/ClientConfig.hpp"
#include "platform/CredentialStore.hpp"
#include "storage/LocalStore.hpp"
#include "voice/VoiceEngine.hpp"

#include <QHash>
#include <QObject>
#include <QTimer>

#include <functional>
#include <memory>
#include <set>

namespace omachat::daemon {

struct DaemonOptions {
    QString socketPath;
    QString databasePath;
    QString configPath;
    bool memoryCredentials = false;
    bool nullAudio = false;
    bool notifications = true;
};

// omachatd: owns the server session, voice engine and local IPC. The GUI,
// CLI and Omarchy plugin are all clients of this object via the socket.
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

    // voice
    void joinVoice(Id channelId, const Responder* r);
    void leaveVoice(const Responder* r);
    void setSelfVoiceState(bool mute, bool deafen);
    void onVoiceSession(const proto::VoiceSession& session);
    void checkVoiceAfterSync();
    voice::VoiceEngine::Settings engineSettings() const;

    // connection / account
    void startAccount(const Account& account, ServerConnection::Credentials creds = {});
    void onModelEvent(const QString& name, const QJsonObject& data);
    void maybeNotify(const QJsonObject& message);
    void scheduleStatus();

    void applyConfig();
    bool saveConfig(QString* error);

    DaemonOptions m_options;
    config::ClientConfig m_config;
    LocalStore m_store;
    std::unique_ptr<ICredentialStore> m_credentials;
    std::unique_ptr<ServerConnection> m_conn;
    std::unique_ptr<audio::AudioBackend> m_audio;
    std::unique_ptr<voice::VoiceEngine> m_voice;
    IpcServer m_ipc;
    Notifier m_notifier;
    QHash<QString, Method> m_methods;

    // voice state owned by the daemon
    Id m_voiceChannel = 0; // channel we intend to be in
    bool m_voicePending = false;
    bool m_selfMute = false;
    bool m_selfDeaf = false;
    QLocalSocket* m_pttOwner = nullptr;
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
