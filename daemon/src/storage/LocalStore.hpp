#pragma once

#include "notifications/NotificationPolicy.hpp"

#include <QByteArray>
#include <QSqlDatabase>
#include <QString>

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace omachat::daemon {

struct Account {
    std::int64_t id = 0;
    QString host;
    quint16 port = 6473;
    QString username;
    QString trustedFingerprint; // pinned certificate fingerprint, explicit user trust
    std::int64_t lastUsed = 0;
};

struct SidebarChannelPreferences {
    std::optional<std::int64_t> favoriteRank;
    bool hidden = false;
    bool collapsed = false;
    bool operator==(const SidebarChannelPreferences&) const = default;
};

struct SidebarPreferences {
    std::map<std::uint64_t, SidebarChannelPreferences> channels;
    bool showVoiceParticipants = true;
    bool compact = false;
    bool operator==(const SidebarPreferences&) const = default;
};

// $XDG_DATA_HOME/omachat/omachat.db: known accounts, pinned certificates,
// local per-user volume, muted channels and small UI state. Secrets are
// never stored here (refresh tokens live in the system keyring).
class LocalStore {
public:
    LocalStore();
    ~LocalStore();

    bool open(const QString& path, QString* error);

    std::vector<Account> accounts() const;
    std::optional<Account> account(std::int64_t id);
    std::optional<Account> findAccount(const QString& host, quint16 port, const QString& username);
    std::int64_t addAccount(const Account& a); // returns id, 0 on failure
    bool removeAccount(std::int64_t id);
    bool setTrustedFingerprint(std::int64_t id, const QString& fingerprint);
    bool setUsername(std::int64_t id, const QString& username);
    bool touchAccount(std::int64_t id);

    // Local gain per remote user (0.0 - 2.0), keyed by account + user id.
    std::map<std::uint64_t, double> userVolumes(std::int64_t accountId);
    bool setUserVolume(std::int64_t accountId, std::uint64_t userId, double gain);

    std::set<std::uint64_t> mutedChannels(std::int64_t accountId);
    std::set<std::uint64_t> mutedChannels(std::int64_t accountId, std::int64_t nowMs);
    bool setChannelMuted(std::int64_t accountId, std::uint64_t channelId, bool muted);

    // nullopt reports a read failure; an absent row has default preferences.
    std::optional<std::map<std::uint64_t, ChannelPreferences>> channelPreferences(std::int64_t accountId);
    std::optional<ChannelPreferences> channelPreferences(std::int64_t accountId, std::uint64_t channelId);
    bool setChannelPreferences(std::int64_t accountId, std::uint64_t channelId, const ChannelPreferences& preferences);

    std::optional<SidebarPreferences> sidebarPreferences(std::int64_t accountId);
    bool setSidebarPreferences(std::int64_t accountId, const SidebarPreferences& preferences);
    bool clearChannelPreferences(std::int64_t accountId, std::uint64_t channelId);

    // End-to-end encryption: the device keys pinned for a contact.
    struct KnownKeys {
        std::vector<QByteArray> keys;
        bool verified = false; // safety number compared for exactly these keys
    };
    KnownKeys knownKeys(std::int64_t accountId, std::uint64_t userId);
    bool setKnownKeys(std::int64_t accountId, std::uint64_t userId, const std::vector<QByteArray>& keys, bool verified);

    QString value(const QString& key, const QString& fallback = {});
    bool setValue(const QString& key, const QString& value);

private:
    bool exec(const QString& sql);
    QString m_connection;
    QSqlDatabase m_db;
};

} // namespace omachat::daemon
