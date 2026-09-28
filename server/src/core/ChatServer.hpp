#pragma once

#include "config/ServerConfig.hpp"
#include "core/EventLog.hpp"
#include "core/State.hpp"
#include "network.pb.h"
#include "omachat/core/RateLimiter.hpp"
#include "omachat/core/Snowflake.hpp"
#include "omachat/protocol/ProtocolConnection.hpp"
#include "storage/Store.hpp"
#include "transport/Certificates.hpp"
#include "voice/MediaRelay.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QSslServer>
#include <QTimer>

#include <map>
#include <memory>
#include <unordered_map>

namespace omachat::server {

class ChatServer : public QObject {
    Q_OBJECT
public:
    ChatServer(ServerConfig config, QObject* parent = nullptr);
    ~ChatServer() override;

    // Opens storage, loads state and starts listening. Returns false with a
    // human-readable reason on failure.
    bool start(const TlsIdentity& identity, QString* error);

    quint16 controlPort() const;
    quint16 mediaPort() const { return m_relay.port(); }
    const MediaRelay& relay() const { return m_relay; }

private:
    struct Session {
        quint64 connId = 0;
        protocol::ProtocolConnection* conn = nullptr;
        QHostAddress peer;
        enum class Phase { AwaitHello, AwaitAuth, Ready } phase = Phase::AwaitHello;
        bool authInFlight = false;
        Id userId = 0;
        Id sessionId = 0;
        QString accessToken;
        TokenBucket general{120, 40};
        TokenBucket messages{10, 2};
        TokenBucket typing{4, 0.5};
        TokenBucket presence{5, 0.2};
        TokenBucket invites{5, 0.1};
        TokenBucket history{30, 5};
        TokenBucket uploads{10, 1}; // BeginUpload only; chunks use `transfer`
        TokenBucket transfer{64, 40}; // upload and download chunks
    };

    // An upload in progress. Lives only in memory and dies with the
    // connection that started it.
    struct Upload {
        Id id = 0;
        quint64 connId = 0;
        Id userId = 0;
        Id channelId = 0;
        QString filename;
        QString mimeType;
        std::uint64_t size = 0;
        std::uint64_t received = 0;
        std::unique_ptr<QFile> file;
        std::unique_ptr<QCryptographicHash> hash;
        std::int64_t lastActivity = 0;
    };

    struct AccessGrant {
        Id userId = 0;
        Id sessionId = 0;
        std::int64_t expiresAt = 0;
    };

    struct VoiceRec {
        Id channelId = 0;
        bool selfMute = false;
        bool selfDeaf = false;
        bool serverMute = false;
        bool serverDeaf = false;
        std::uint32_t streamId = 0;
    };

    // ---- connection lifecycle
    void onPendingConnection();
    void onEnvelope(quint64 connId, const proto::Envelope& env);
    void onClosed(quint64 connId);
    Session* sessionFor(quint64 connId);

    // ---- replies
    void reply(Session& s, std::uint64_t requestId, proto::Envelope&& env);
    void replyOk(Session& s, std::uint64_t requestId);
    void replyError(
        Session& s, std::uint64_t requestId, proto::ErrorCode code, const QString& message, int retryAfterMs = 0);
    bool limit(Session& s, std::uint64_t requestId, TokenBucket& bucket);

    // ---- events
    void publish(proto::Event event, std::vector<Id> recipients);
    void publishEphemeral(const proto::Event& event, const std::vector<Id>& recipients, Id except = 0);
    void deliver(Id userId, const proto::Event& event);
    proto::Event personalize(const proto::Event& event, Id userId) const;

    // ---- conversions
    proto::User toProto(const UserRecord& u) const;
    proto::Channel toProto(const ChannelRecord& c, Id viewer) const;
    proto::Role toProto(const RoleRecord& r) const;
    proto::Member toProto(const MemberRecord& m) const;
    proto::ChatMessage toProto(const MessageRecord& m, Id viewer);
    proto::VoiceState toProto(Id userId, const VoiceRec& v) const;
    proto::UserStatus statusOf(Id userId) const;

    // ---- handlers: auth (AuthHandlers.cpp)
    void handleHello(Session& s, std::uint64_t rid, const proto::Hello& m);
    void handleRegister(Session& s, std::uint64_t rid, const proto::RegisterRequest& m);
    void handleLogin(Session& s, std::uint64_t rid, const proto::LoginRequest& m);
    void handleRefresh(Session& s, std::uint64_t rid, const proto::RefreshRequest& m);
    void handleResume(Session& s, std::uint64_t rid, const proto::ResumeRequest& m);
    void handleLogout(Session& s, std::uint64_t rid);
    void completeAuth(Session& s, std::uint64_t rid, Id userId, Id sessionId, const QString& refreshToken);
    void attachUser(Session& s, Id userId, Id sessionId, const QString& accessToken);

    // ---- handlers: servers & channels (ServerHandlers.cpp)
    void handleSync(Session& s, std::uint64_t rid);
    void handleCreateServer(Session& s, std::uint64_t rid, const proto::CreateServerRequest& m);
    void handleLeaveServer(Session& s, std::uint64_t rid, const proto::LeaveServerRequest& m);
    void handleDeleteServer(Session& s, std::uint64_t rid, const proto::DeleteServerRequest& m);
    void handleCreateInvite(Session& s, std::uint64_t rid, const proto::CreateInviteRequest& m);
    void handleListInvites(Session& s, std::uint64_t rid, const proto::ListInvitesRequest& m);
    void handleJoinInvite(Session& s, std::uint64_t rid, const proto::JoinInviteRequest& m);
    void handleCreateChannel(Session& s, std::uint64_t rid, const proto::CreateChannelRequest& m);
    void handleUpdateChannel(Session& s, std::uint64_t rid, const proto::UpdateChannelRequest& m);
    void handleDeleteChannel(Session& s, std::uint64_t rid, const proto::DeleteChannelRequest& m);
    void handleOpenDm(Session& s, std::uint64_t rid, const proto::OpenDmRequest& m);

    // ---- handlers: messages & presence (MessageHandlers.cpp)
    void handleSendMessage(Session& s, std::uint64_t rid, const proto::SendMessageRequest& m);
    void handleEditMessage(Session& s, std::uint64_t rid, const proto::EditMessageRequest& m);
    void handleDeleteMessage(Session& s, std::uint64_t rid, const proto::DeleteMessageRequest& m);
    void handleGetMessages(Session& s, std::uint64_t rid, const proto::GetMessagesRequest& m);
    void handleSearch(Session& s, std::uint64_t rid, const proto::SearchMessagesRequest& m);
    void handleReaction(Session& s, std::uint64_t rid, const proto::ReactionRequest& m);
    void handleTyping(Session& s, std::uint64_t rid, const proto::TypingRequest& m);
    void handleSetPresence(Session& s, std::uint64_t rid, const proto::SetPresenceRequest& m);
    std::vector<Id> extractMentions(Id channelId, const QString& content) const;

    // ---- handlers: attachments (AttachmentHandlers.cpp)
    void handleBeginUpload(Session& s, std::uint64_t rid, const proto::BeginUploadRequest& m);
    void handleUploadChunk(Session& s, std::uint64_t rid, const proto::UploadChunkRequest& m);
    void handleFinishUpload(Session& s, std::uint64_t rid, const proto::FinishUploadRequest& m);
    void handleCancelUpload(Session& s, std::uint64_t rid, const proto::CancelUploadRequest& m);
    void handleDownload(Session& s, std::uint64_t rid, const proto::DownloadRequest& m);
    Upload* uploadFor(Session& s, std::uint64_t rid, Id attachmentId);
    void abortUpload(Id uploadId);
    void abortUploadsOf(quint64 connId);
    void removeAttachmentFiles(const std::vector<Id>& ids);
    void collectAttachmentGarbage();
    QString attachmentPath(Id id) const;
    std::uint64_t maxUploadBytes() const;

    // ---- handlers: voice (VoiceHandlers.cpp)
    void handleJoinVoice(Session& s, std::uint64_t rid, const proto::JoinVoiceRequest& m);
    void handleLeaveVoice(Session& s, std::uint64_t rid);
    void handleSetVoiceState(Session& s, std::uint64_t rid, const proto::SetVoiceStateRequest& m);
    void handleServerMute(Session& s, std::uint64_t rid, const proto::ServerMuteRequest& m);
    void leaveVoice(Id userId);
    void refreshVoicePermissions();
    void publishVoiceState(Id userId, const VoiceRec& v, Id channelForAudience);

    // ---- handlers: moderation & roles (ModerationHandlers.cpp)
    void handleKick(Session& s, std::uint64_t rid, const proto::KickRequest& m);
    void handleBan(Session& s, std::uint64_t rid, const proto::BanRequest& m);
    void handleUnban(Session& s, std::uint64_t rid, const proto::UnbanRequest& m);
    void handleCreateRole(Session& s, std::uint64_t rid, const proto::CreateRoleRequest& m);
    void handleUpdateRole(Session& s, std::uint64_t rid, const proto::UpdateRoleRequest& m);
    void handleDeleteRole(Session& s, std::uint64_t rid, const proto::DeleteRoleRequest& m);
    void handleAssignRole(Session& s, std::uint64_t rid, const proto::AssignRoleRequest& m);
    void handleSetOverride(Session& s, std::uint64_t rid, const proto::SetOverrideRequest& m);
    void handleListOverrides(Session& s, std::uint64_t rid, const proto::ListOverridesRequest& m);
    void removeMemberFromServer(Id serverId, Id userId, const QString& reason);
    void publishPermissionsChanged(Id serverId);

    // ---- presence
    void userCameOnline(Id userId);
    void scheduleOffline(Id userId);

    std::int64_t now() const;

    ServerConfig m_config;
    Store m_store;
    State m_state;
    EventLog m_events;
    SnowflakeGenerator m_ids;
    MediaRelay m_relay;
    QSslServer m_listener;

    quint64 m_nextConnId = 1;
    std::unordered_map<quint64, std::unique_ptr<Session>> m_sessions;
    std::multimap<Id, quint64> m_userConns;
    QHash<QString, AccessGrant> m_accessTokens;
    std::map<Id, proto::UserStatus> m_chosenStatus;
    std::map<Id, VoiceRec> m_voice;
    std::map<Id, QTimer*> m_offlineTimers;
    QHash<QString, int> m_connectionsPerIp;
    std::map<Id, Upload> m_uploads;

    // Brute-force protection, keyed by peer address and by username.
    std::map<QString, TokenBucket> m_authByIp;
    std::map<QString, TokenBucket> m_authByName;
    std::map<QString, TokenBucket> m_registerByIp;
    QTimer m_housekeeping;
};

} // namespace omachat::server
