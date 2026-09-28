#pragma once

#include "network.pb.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace omachat::daemon {

using Id = std::uint64_t;

// Local IPC event emitted when the model changes.
struct ModelEvent {
    QString name;
    QJsonObject data;
};

// The daemon's mirror of everything the server told this user. The server is
// authoritative; this is rebuilt from SyncState and advanced by events.
class ClientState {
public:
    void reset(const proto::SyncState& sync);
    void clear();

    // Applies a pushed event. Appends IPC notifications to `out` and sets
    // `needsResync` when the change cannot be applied incrementally.
    void apply(const proto::Event& event, std::vector<ModelEvent>& out, bool& needsResync);

    // Direct updates from request replies (e.g. created channel).
    void upsertChannel(const proto::Channel& c);

    bool valid() const { return m_valid; }
    const proto::User& self() const { return m_self; }
    std::uint64_t lastSequence() const { return m_lastSequence; }
    void setLastSequence(std::uint64_t seq) { m_lastSequence = std::max(m_lastSequence, seq); }

    const proto::Channel* channel(Id id) const;
    const proto::Server* server(Id id) const;
    const proto::User* user(Id id) const;
    const proto::VoiceState* voiceState(Id userId) const;
    const std::map<Id, proto::Server>& servers() const { return m_servers; }
    const std::map<Id, proto::Channel>& channels() const { return m_channels; }
    const std::map<Id, proto::VoiceState>& voiceStates() const { return m_voice; }
    std::vector<Id> voiceParticipants(Id channelId) const;
    std::uint64_t serverPermissions(Id serverId) const;

    // Finds a channel by id, or by name ("general", "#general",
    // "Server/general"). Exact-case matches win over case-insensitive ones;
    // `kind` narrows the search. Returns 0 if not found or ambiguous.
    enum class ChannelKind { Any, Messages, Voice };
    Id resolveChannel(const QString& ref, ChannelKind kind = ChannelKind::Any) const;
    Id resolveServer(const QString& ref) const;
    Id resolveUser(const QString& ref) const;

    // ---- JSON projections (ids are decimal strings)
    QJsonObject snapshotJson() const;
    QJsonObject userJson(const proto::User& u) const;
    QJsonObject serverJson(const proto::Server& s) const;
    QJsonObject channelJson(const proto::Channel& c) const;
    QJsonObject roleJson(const proto::Role& r) const;
    QJsonObject memberJson(const proto::Member& m) const;
    QJsonObject voiceStateJson(const proto::VoiceState& v) const;
    QJsonObject messageJson(const proto::ChatMessage& m) const;
    static QJsonObject attachmentJson(const proto::Attachment& a);

private:
    bool m_valid = false;
    proto::User m_self;
    std::map<Id, proto::Server> m_servers;
    std::map<Id, proto::Channel> m_channels;
    std::map<Id, proto::Role> m_roles;
    std::map<std::pair<Id, Id>, proto::Member> m_members; // (server, user)
    std::map<Id, proto::User> m_users;
    std::map<Id, proto::VoiceState> m_voice; // by user
    std::map<Id, std::uint64_t> m_serverPermissions;
    std::uint64_t m_lastSequence = 0;
};

QString idString(Id id);
Id idFromJson(const QJsonValue& v); // accepts "123" or 123
QString statusName(proto::UserStatus s);
proto::UserStatus statusFromName(const QString& s, bool* ok = nullptr);
QString channelTypeName(proto::ChannelType t);

} // namespace omachat::daemon
