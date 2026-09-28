#include "crypto/E2EManager.hpp"

#include "omachat/core/Log.hpp"

#include <algorithm>

namespace omachat::daemon {

namespace {

QString secretKey(std::int64_t account)
{
    return QStringLiteral("e2e/%1").arg(account);
}

bool conversation(const proto::Channel& c)
{
    return c.type() == proto::CHANNEL_TYPE_DM || c.type() == proto::CHANNEL_TYPE_GROUP_DM;
}

std::vector<QByteArray> sorted(std::vector<QByteArray> keys)
{
    std::ranges::sort(keys);
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return keys;
}

} // namespace

E2EManager::E2EManager(ServerConnection& conn, ICredentialStore& credentials, LocalStore& store, QObject* parent)
    : QObject(parent)
    , m_conn(conn)
    , m_credentials(credentials)
    , m_store(store)
{
}

bool E2EManager::active() const
{
    return m_identity && m_conn.capabilities().contains(QStringLiteral("e2e.v1"));
}

bool E2EManager::appliesTo(Id channelId) const
{
    const proto::Channel* c = m_conn.model().channel(channelId);
    return c && conversation(*c) && m_conn.capabilities().contains(QStringLiteral("e2e.v1"));
}

void E2EManager::load(std::int64_t accountId)
{
    if (accountId == m_account && (m_identity || m_loading))
        return;
    m_account = accountId;
    m_identity.reset();
    m_directory.clear();
    if (!accountId)
        return;
    m_loading = true;
    m_credentials.read(secretKey(accountId), [this, accountId](bool ok, const QString& secret, const QString& error) {
        if (accountId != m_account)
            return;
        m_loading = false;
        if (!ok) {
            OMA_WARN("e2e", "keyring unavailable; encrypted conversations are disabled", {"error", error});
            return;
        }
        if (!secret.isEmpty()) {
            m_identity = e2e::Identity::fromSecret(QByteArray::fromBase64(secret.toLatin1()));
            if (!m_identity)
                OMA_WARN("e2e", "stored device key is unreadable; creating a new one");
        }
        if (!m_identity) {
            m_identity = e2e::Identity::generate();
            m_credentials.write(secretKey(accountId), QString::fromLatin1(m_identity->secretBytes().toBase64()),
                [](bool written, const QString& why) {
                    if (!written)
                        OMA_WARN("e2e", "could not store the device key", {"error", why});
                });
            OMA_INFO("e2e", "created a device key for this account");
        }
        emit readyChanged();
        if (m_conn.state() == ServerConnection::State::Connected)
            onSynchronized();
    });
}

void E2EManager::forget()
{
    if (m_identity && m_conn.state() == ServerConnection::State::Connected) {
        proto::Envelope env;
        env.mutable_revoke_device_key()->set_public_key(m_identity->publicBytes().toStdString());
        m_conn.request(std::move(env), [](const proto::Envelope&) { });
    }
    if (m_account)
        m_credentials.remove(secretKey(m_account), {});
    m_identity.reset();
    m_account = 0;
}

void E2EManager::publishOwnKey()
{
    proto::Envelope env;
    env.mutable_publish_device_key()->set_public_key(m_identity->publicBytes().toStdString());
    m_conn.request(std::move(env), [](const proto::Envelope& reply) {
        if (reply.has_error())
            OMA_WARN(
                "e2e", "could not publish the device key", {"error", QString::fromStdString(reply.error().message())});
    });
}

std::vector<E2EManager::Id> E2EManager::participants(Id channelId) const
{
    std::vector<Id> out;
    if (const proto::Channel* c = m_conn.model().channel(channelId))
        out.assign(c->recipient_ids().begin(), c->recipient_ids().end());
    return out;
}

void E2EManager::onSynchronized()
{
    if (!active())
        return;
    publishOwnKey();
    std::set<Id> users{m_conn.model().self().id()};
    for (const auto& [id, c] : m_conn.model().channels()) {
        if (conversation(c))
            users.insert(c.recipient_ids().begin(), c.recipient_ids().end());
    }
    // The directory answers at most 50 users per request.
    std::vector<Id> batch;
    for (Id u : users) {
        batch.push_back(u);
        if (batch.size() == 50) {
            fetch(std::exchange(batch, {}), {});
        }
    }
    if (!batch.empty())
        fetch(batch, {});
}

void E2EManager::onDeviceKeysChanged(Id userId)
{
    if (active())
        fetch({userId}, {});
}

void E2EManager::onChannel(const proto::Channel& c)
{
    if (!active() || !conversation(c))
        return;
    std::vector<Id> missing;
    for (Id u : c.recipient_ids()) {
        if (!m_directory.contains(u))
            missing.push_back(u);
    }
    if (!missing.empty())
        fetch(missing, {});
}

void E2EManager::fetch(std::vector<Id> users, std::function<void(const QString& error)> done)
{
    proto::Envelope env;
    for (Id u : users)
        env.mutable_get_device_keys()->add_user_ids(u);
    const std::int64_t account = m_account;
    m_conn.request(std::move(env), [this, users, account, done](const proto::Envelope& reply) {
        if (account != m_account)
            return;
        if (reply.has_error()) {
            if (done)
                done(QString::fromStdString(reply.error().message()));
            return;
        }
        std::map<Id, std::vector<QByteArray>> found;
        for (const auto& k : reply.device_key_list().keys())
            found[k.user_id()].push_back(QByteArray::fromStdString(k.public_key()));
        for (Id u : users) {
            const auto keys = sorted(found[u]);
            m_directory[u] = keys;
            const auto pinned = m_store.knownKeys(m_account, u);
            if (pinned.keys.empty() && !keys.empty()) {
                m_store.setKnownKeys(m_account, u, keys, false); // first contact: trust on first use
            } else if (!pinned.keys.empty() && sorted(pinned.keys) != keys) {
                // Someone added or removed a device (or the server lies): the
                // safety number changed and any earlier verification is void.
                m_store.setKnownKeys(m_account, u, keys, false);
                if (!(u == m_conn.model().self().id() && keys.size() == 1 && m_identity
                        && keys.front() == m_identity->publicBytes()))
                    emit keysChanged(u);
            }
        }
        if (done)
            done({});
    });
}

E2EManager::Decrypted E2EManager::decrypt(const proto::ChatMessage& m)
{
    Decrypted out;
    out.status = QStringLiteral("undecryptable");
    if (!m_identity)
        return out;
    const auto opened = e2e::open(m.encrypted(), {m.channel_id(), m.author_id()}, *m_identity);
    if (!opened)
        return out;
    out.content = QString::fromStdString(opened->body.content());
    out.files.assign(opened->body.files().begin(), opened->body.files().end());
    std::vector<QByteArray> known = m_store.knownKeys(m_account, m.author_id()).keys;
    if (auto it = m_directory.find(m.author_id()); it != m_directory.end())
        known.insert(known.end(), it->second.begin(), it->second.end());
    if (m.author_id() == m_conn.model().self().id())
        known.push_back(m_identity->publicBytes());
    out.status = std::ranges::find(known, opened->senderKey) != known.end() ? QStringLiteral("ok")
                                                                            : QStringLiteral("unverified");
    for (const auto& f : out.files)
        m_files[f.attachment_id()] = f;
    m_messageFiles[m.id()] = out.files;
    m_messageChannels[m.id()] = m.channel_id();
    return out;
}

void E2EManager::seal(Id channelId, proto::E2EBody body, SealDone done)
{
    if (!m_identity) {
        done(std::nullopt, QStringLiteral("this device's encryption key is not ready yet"));
        return;
    }
    const std::vector<Id> people = participants(channelId);
    std::vector<Id> missing;
    for (Id u : people) {
        if (!m_directory.contains(u))
            missing.push_back(u);
    }
    auto finish = [this, channelId, people, body = std::move(body), done](const QString& error) {
        if (!error.isEmpty()) {
            done(std::nullopt, QStringLiteral("cannot look up encryption keys: %1").arg(error));
            return;
        }
        const Id self = m_conn.model().self().id();
        std::vector<QByteArray> recipients{m_identity->publicBytes()};
        for (Id u : people) {
            const auto& keys = m_directory[u];
            if (keys.empty() && u != self) {
                const proto::User* user = m_conn.model().user(u);
                done(std::nullopt,
                    QStringLiteral("%1 has not set up encryption yet, so this conversation cannot be sent to "
                                   "them securely (they need OmaChat 0.2 or newer)")
                        .arg(user ? QString::fromStdString(user->display_name()) : QStringLiteral("Someone")));
                return;
            }
            recipients.insert(recipients.end(), keys.begin(), keys.end());
        }
        auto payload = e2e::seal(body, {channelId, self}, *m_identity, recipients);
        if (!payload)
            done(std::nullopt, QStringLiteral("encryption failed"));
        else
            done(std::move(payload), {});
    };
    if (missing.empty())
        finish({});
    else
        fetch(missing, finish);
}

std::optional<proto::E2EFile> E2EManager::fileFor(Id attachmentId) const
{
    auto it = m_files.find(attachmentId);
    return it == m_files.end() ? std::nullopt : std::optional<proto::E2EFile>(it->second);
}

std::vector<proto::E2EFile> E2EManager::filesOf(Id messageId) const
{
    auto it = m_messageFiles.find(messageId);
    return it == m_messageFiles.end() ? std::vector<proto::E2EFile>{} : it->second;
}

std::optional<std::uint64_t> E2EManager::channelOf(Id messageId) const
{
    auto it = m_messageChannels.find(messageId);
    return it == m_messageChannels.end() ? std::nullopt : std::optional<std::uint64_t>(it->second);
}

QJsonObject E2EManager::safetyJson(Id userId) const
{
    const Id self = m_conn.model().self().id();
    auto keysOf = [this](Id u) {
        auto it = m_directory.find(u);
        return it != m_directory.end() ? it->second : m_store.knownKeys(m_account, u).keys;
    };
    std::vector<QByteArray> mine = keysOf(self);
    if (m_identity && std::ranges::find(mine, m_identity->publicBytes()) == mine.end())
        mine.push_back(m_identity->publicBytes());
    const std::vector<QByteArray> theirs = keysOf(userId);
    const auto pinned = m_store.knownKeys(m_account, userId);
    return {{"user_id", QString::number(userId)}, {"number", e2e::safetyNumber(self, mine, userId, theirs)},
        {"devices", static_cast<int>(theirs.size())},
        {"verified", pinned.verified && sorted(pinned.keys) == sorted(theirs)}};
}

bool E2EManager::setVerified(Id userId, bool verified)
{
    auto it = m_directory.find(userId);
    const std::vector<QByteArray> keys
        = it != m_directory.end() ? it->second : m_store.knownKeys(m_account, userId).keys;
    if (keys.empty())
        return false;
    return m_store.setKnownKeys(m_account, userId, keys, verified);
}

QJsonObject E2EManager::statusJson() const
{
    QString device;
    if (m_identity) {
        const QByteArray hex = m_identity->publicBytes().left(8).toHex().toUpper();
        for (int i = 0; i < hex.size(); i += 4)
            device += (i ? QStringLiteral(" ") : QString()) + QString::fromLatin1(hex.mid(i, 4));
    }
    return {{"enabled", active()}, {"ready", ready()}, {"device", device}};
}

} // namespace omachat::daemon
