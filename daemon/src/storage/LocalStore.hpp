#pragma once

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

// $XDG_DATA_HOME/omachat/omachat.db: known accounts, pinned certificates,
// local per-user volume, muted channels and small UI state. Secrets are
// never stored here (refresh tokens live in the system keyring).
class LocalStore {
public:
    LocalStore();
    ~LocalStore();

    bool open(const QString& path, QString* error);

    std::vector<Account> accounts();
    std::optional<Account> account(std::int64_t id);
    std::optional<Account> findAccount(const QString& host, quint16 port, const QString& username);
    std::int64_t addAccount(const Account& a); // returns id, 0 on failure
    bool removeAccount(std::int64_t id);
    bool setTrustedFingerprint(std::int64_t id, const QString& fingerprint);
    bool touchAccount(std::int64_t id);

    // Local gain per remote user (0.0 - 2.0), keyed by account + user id.
    std::map<std::uint64_t, double> userVolumes(std::int64_t accountId);
    bool setUserVolume(std::int64_t accountId, std::uint64_t userId, double gain);

    std::set<std::uint64_t> mutedChannels(std::int64_t accountId);
    bool setChannelMuted(std::int64_t accountId, std::uint64_t channelId, bool muted);

    QString value(const QString& key, const QString& fallback = {});
    bool setValue(const QString& key, const QString& value);

private:
    bool exec(const QString& sql);
    QString m_connection;
    QSqlDatabase m_db;
};

} // namespace omachat::daemon
