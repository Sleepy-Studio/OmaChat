#pragma once

#include "crypto/E2E.hpp"
#include "networking/ServerConnection.hpp"
#include "platform/CredentialStore.hpp"
#include "storage/LocalStore.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

#include <functional>
#include <map>
#include <optional>
#include <set>

namespace omachat::daemon {

// End-to-end encryption for one account (see e2e::seal for the scheme).
//
// - The device's identity key pair lives in the keyring ("e2e/<account>");
//   only its public half is published, once per sync.
// - Participants' device keys come from the server's directory and are
//   pinned locally on first use. A later change (a new or removed device)
//   is reported as keysChanged so the user can compare safety numbers.
// - Direct and group conversations are always encrypted when the server
//   supports it; a participant without keys makes sending fail rather than
//   silently falling back to plaintext.
class E2EManager : public QObject {
    Q_OBJECT
public:
    E2EManager(ServerConnection& conn, ICredentialStore& credentials, LocalStore& store, QObject* parent = nullptr);

    // Loads (or creates) this account's device key. Idempotent.
    void load(std::int64_t accountId);
    void forget(); // account removed: revoke the key and delete the secret

    bool ready() const { return m_identity.has_value(); }
    // The server supports it and this device has its key.
    bool active() const;
    bool appliesTo(std::uint64_t channelId) const; // an encrypted conversation

    void onSynchronized();
    void onDeviceKeysChanged(std::uint64_t userId);
    void onChannel(const proto::Channel& c); // a new conversation: learn its people's keys

    struct Decrypted {
        QString content;
        std::vector<proto::E2EFile> files;
        QString status; // ok | unverified | undecryptable
    };
    Decrypted decrypt(const proto::ChatMessage& m);

    using SealDone = std::function<void(std::optional<std::string> payload, const QString& error)>;
    void seal(std::uint64_t channelId, proto::E2EBody body, SealDone done);

    // Keys for attachments this device has seen in decrypted messages.
    std::optional<proto::E2EFile> fileFor(std::uint64_t attachmentId) const;
    std::vector<proto::E2EFile> filesOf(std::uint64_t messageId) const;
    // Channel of an encrypted message this device decrypted (edits re-encrypt).
    std::optional<std::uint64_t> channelOf(std::uint64_t messageId) const;

    QJsonObject safetyJson(std::uint64_t userId) const;
    bool setVerified(std::uint64_t userId, bool verified);
    QJsonObject statusJson() const;

signals:
    // A contact's devices changed after we had pinned them.
    void keysChanged(quint64 userId);
    void readyChanged();

private:
    using Id = std::uint64_t;
    void publishOwnKey();
    void fetch(std::vector<Id> users, std::function<void(const QString& error)> done);
    std::vector<Id> participants(Id channelId) const;

    ServerConnection& m_conn;
    ICredentialStore& m_credentials;
    LocalStore& m_store;
    std::int64_t m_account = 0;
    std::optional<e2e::Identity> m_identity;
    bool m_loading = false;
    std::map<Id, std::vector<QByteArray>> m_directory; // user -> current device keys
    std::map<Id, proto::E2EFile> m_files; // attachment id -> key
    std::map<Id, std::vector<proto::E2EFile>> m_messageFiles; // message id -> files
    std::map<Id, Id> m_messageChannels; // encrypted message id -> channel
};

} // namespace omachat::daemon
