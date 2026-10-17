#include "storage/SendRecoveryStore.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <sodium.h>

#include <deque>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace omachat::daemon {
namespace {
using Binding = SendRecoveryStore::Binding;
using Record = SendRecoveryStore::Record;
using State = SendRecoveryStore::State;
constexpr int kOverhead
    = crypto_secretstream_xchacha20poly1305_HEADERBYTES + crypto_secretstream_xchacha20poly1305_ABYTES;
QString accountHash(const QString& id)
{
    return QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex());
}
QJsonObject bindingJson(const Binding& b)
{
    return {{"account", b.accountId}, {"endpoint", b.endpoint}, {"pin", b.certificatePin}, {"user", b.userId}};
}
bool validBinding(const Binding& b)
{
    return !b.accountId.isEmpty() && !b.endpoint.isEmpty() && !b.certificatePin.isEmpty() && !b.userId.isEmpty();
}
QByteArray encode(const Binding& b, const QVector<Record>& records)
{
    QJsonArray items;
    for (const auto& r : records) {
        QJsonArray paths;
        for (const auto& p : r.attachmentPaths)
            paths.append(p);
        items.append(QJsonObject{{"id", r.id}, {"channel", r.channelId}, {"content", r.content}, {"paths", paths},
            {"original", r.original}, {"reply", r.replyTo}, {"action", r.action},
            {"created", r.createdAt.toUTC().toString(Qt::ISODateWithMs)}, {"state", static_cast<int>(r.state)}});
    }
    return QJsonDocument(QJsonObject{{"version", 1}, {"binding", bindingJson(b)}, {"records", items}})
        .toJson(QJsonDocument::Compact);
}
bool decode(const QByteArray& plain, const Binding& b, QVector<Record>& records)
{
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(plain, &error);
    const auto root = doc.object();
    if (error.error != QJsonParseError::NoError || root.value("version").toInt() != 1
        || root.value("binding").toObject() != bindingJson(b) || !root.value("records").isArray())
        return false;
    const auto items = root.value("records").toArray();
    if (items.size() > SendRecoveryStore::kMaxRecords)
        return false;
    QSet<QString> ids;
    for (const auto& item : items) {
        if (!item.isObject())
            return false;
        const auto o = item.toObject();
        Record r;
        r.id = o.value("id").toString();
        r.channelId = o.value("channel").toString();
        r.content = o.value("content").toString();
        r.original = o.value("original").toString();
        r.replyTo = o.value("reply").toString();
        r.action = o.value("action").toBool();
        r.createdAt = QDateTime::fromString(o.value("created").toString(), Qt::ISODateWithMs);
        const int state = o.value("state").toInt(-1);
        if (r.id.isEmpty() || r.channelId.isEmpty() || !r.createdAt.isValid() || ids.contains(r.id) || state < 0
            || state > 2 || !o.value("paths").isArray() || !o.value("content").isString()
            || !o.value("original").isString() || !o.value("reply").isString() || !o.value("action").isBool())
            return false;
        r.state = static_cast<State>(state);
        for (const auto& path : o.value("paths").toArray()) {
            if (!path.isString() || path.toString().isEmpty())
                return false;
            r.attachmentPaths.append(path.toString());
        }
        ids.insert(r.id);
        records.append(std::move(r));
    }
    return true;
}
} // namespace

struct SendRecoveryStore::Impl : std::enable_shared_from_this<Impl> {
    QString directory;
    ICredentialStore& credentials;
    std::deque<std::function<void()>> queue;
    bool running = false;
    bool alive = true;
    QSet<QString> retired;
    QMap<QString, quint64> generations;
    QSet<QString> dismissed;
    QSet<QString> deleting;
    QSet<QString> livePending;

    Impl(QString dir, ICredentialStore& store)
        : directory(std::move(dir))
        , credentials(store)
    {
    }
    QString path(const QString& account) const { return QDir(directory).filePath(accountHash(account) + ".recovery"); }
    QString keyName(const QString& account) const { return "send-recovery-v1/" + accountHash(account); }
    void enqueue(std::function<void()> operation)
    {
        queue.push_back(std::move(operation));
        if (!running) {
            running = true;
            queue.front()();
        }
    }
    void finish()
    {
        if (queue.empty())
            return;
        queue.pop_front();
        if (queue.empty())
            running = false;
        else
            queue.front()();
    }
    using Ready = std::function<void(QByteArray, QVector<Record>, QString)>;
    void read(const Binding& b, quint64 generation, Ready done, bool create = true)
    {
        if (!validBinding(b) || generations.value(b.accountId) != generation || retired.contains(b.accountId)
            || credentials.backendName() != "keyring" || sodium_init() < 0) {
            done({}, {}, "Secure send recovery is unavailable for this account");
            return;
        }
        const QFileInfo root(directory);
        struct stat directoryStat{};
        if (root.exists()
            && (root.isSymLink() || root.canonicalFilePath() != QDir::cleanPath(root.absoluteFilePath())
                || ::lstat(QFile::encodeName(directory).constData(), &directoryStat) != 0
                || !S_ISDIR(directoryStat.st_mode) || directoryStat.st_uid != ::geteuid()
                || (directoryStat.st_mode & 0777) != 0700)) {
            done({}, {}, "Send recovery directory is not private and owned by this user");
            return;
        }
        if (!create && !QFileInfo::exists(path(b.accountId)) && !QFileInfo(path(b.accountId)).isSymLink()) {
            done({}, {}, {});
            return;
        }
        auto self = shared_from_this();
        credentials.read(
            keyName(b.accountId), [self, b, generation, done, create](bool ok, const QString& secret, const QString&) {
                if (!self->alive)
                    return;
                if (!ok || self->generations.value(b.accountId) != generation || self->retired.contains(b.accountId)) {
                    done({}, {}, "Cannot read the secure send recovery key");
                    return;
                }
                auto key = QByteArray::fromBase64(secret.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
                const bool exists
                    = QFileInfo::exists(self->path(b.accountId)) || QFileInfo(self->path(b.accountId)).isSymLink();
                if (secret.isEmpty() && !exists) {
                    if (!create) {
                        done({}, {}, {});
                        return;
                    }
                    key.resize(crypto_secretstream_xchacha20poly1305_KEYBYTES);
                    randombytes_buf(key.data(), static_cast<size_t>(key.size()));
                    self->credentials.write(self->keyName(b.accountId), QString::fromLatin1(key.toBase64()),
                        [self, b, generation, key, done](bool written, const QString&) mutable {
                            if (!self->alive) {
                                sodium_memzero(key.data(), static_cast<size_t>(key.size()));
                                return;
                            }
                            if (!written || self->generations.value(b.accountId) != generation
                                || self->retired.contains(b.accountId))
                                done({}, {}, "Cannot persist the secure send recovery key");
                            else
                                done(key, {}, {});
                            sodium_memzero(key.data(), static_cast<size_t>(key.size()));
                        });
                    sodium_memzero(key.data(), static_cast<size_t>(key.size()));
                    return;
                }
                if (key.size() != crypto_secretstream_xchacha20poly1305_KEYBYTES) {
                    done({}, {}, "Send recovery key is missing or invalid");
                    return;
                }
                QVector<Record> records;
                QString failure;
                if (exists) {
                    QFile file;
                    const auto native = QFile::encodeName(self->path(b.accountId));
                    const int fd = ::open(native.constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
                    struct stat stat{};
                    bool safe = fd >= 0 && ::fstat(fd, &stat) == 0 && S_ISREG(stat.st_mode)
                        && stat.st_uid == ::geteuid() && (stat.st_mode & 0777) == 0600
                        && stat.st_size <= kMaxBytes + kOverhead && stat.st_size >= kOverhead;
                    if (safe)
                        safe = file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle);
                    if (!safe) {
                        if (fd >= 0)
                            ::close(fd);
                        failure = "Cannot read the bounded private send recovery file";
                    } else {
                        auto cipher = file.read(kMaxBytes + kOverhead + 1);
                        if (cipher.size() != stat.st_size || !file.atEnd()) {
                            done({}, {}, "Send recovery file changed or exceeds capacity");
                            sodium_memzero(key.data(), static_cast<size_t>(key.size()));
                            return;
                        }
                        QByteArray plain(cipher.size() - kOverhead, '\0');
                        crypto_secretstream_xchacha20poly1305_state state;
                        unsigned char tag = 0;
                        unsigned long long length = 0;
                        const bool opened = crypto_secretstream_xchacha20poly1305_init_pull(&state,
                                                reinterpret_cast<const unsigned char*>(cipher.constData()),
                                                reinterpret_cast<const unsigned char*>(key.constData()))
                                == 0
                            && crypto_secretstream_xchacha20poly1305_pull(&state,
                                   reinterpret_cast<unsigned char*>(plain.data()), &length, &tag,
                                   reinterpret_cast<const unsigned char*>(cipher.constData())
                                       + crypto_secretstream_xchacha20poly1305_HEADERBYTES,
                                   static_cast<unsigned long long>(
                                       cipher.size() - crypto_secretstream_xchacha20poly1305_HEADERBYTES),
                                   nullptr, 0)
                                == 0;
                        sodium_memzero(&state, sizeof(state));
                        if (!opened || tag != crypto_secretstream_xchacha20poly1305_TAG_FINAL
                            || length != static_cast<unsigned long long>(plain.size()) || !decode(plain, b, records))
                            failure = "Send recovery authentication or account binding failed";
                        sodium_memzero(plain.data(), static_cast<size_t>(plain.size()));
                    }
                }
                done(failure.isEmpty() ? key : QByteArray(), records, failure);
                sodium_memzero(key.data(), static_cast<size_t>(key.size()));
            });
    }
    bool write(const Binding& b, const QByteArray& key, const QVector<Record>& records, QString& error)
    {
        auto plain = encode(b, records);
        if (plain.size() > kMaxBytes || records.size() > kMaxRecords || retired.contains(b.accountId)) {
            error = "Send recovery capacity exceeded or account retired";
            sodium_memzero(plain.data(), static_cast<size_t>(plain.size()));
            return false;
        }
        QByteArray cipher(plain.size() + kOverhead, '\0');
        crypto_secretstream_xchacha20poly1305_state state;
        bool ok
            = crypto_secretstream_xchacha20poly1305_init_push(&state, reinterpret_cast<unsigned char*>(cipher.data()),
                  reinterpret_cast<const unsigned char*>(key.constData()))
            == 0;
        if (ok)
            ok = crypto_secretstream_xchacha20poly1305_push(&state,
                     reinterpret_cast<unsigned char*>(cipher.data())
                         + crypto_secretstream_xchacha20poly1305_HEADERBYTES,
                     nullptr, reinterpret_cast<const unsigned char*>(plain.constData()),
                     static_cast<unsigned long long>(plain.size()), nullptr, 0,
                     crypto_secretstream_xchacha20poly1305_TAG_FINAL)
                == 0;
        sodium_memzero(&state, sizeof(state));
        sodium_memzero(plain.data(), static_cast<size_t>(plain.size()));
        const QFileInfo root(directory);
        if (!ok || root.isSymLink() || !QDir().mkpath(directory) || QFileInfo(directory).ownerId() != ::geteuid()
            || QFileInfo(directory).canonicalFilePath() != QDir::cleanPath(QFileInfo(directory).absoluteFilePath())
            || QFileInfo(path(b.accountId)).isSymLink()
            || !QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) {
            error = "Cannot encrypt or create send recovery storage";
            return false;
        }
        QSaveFile file(path(b.accountId));
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
            || file.write(cipher) != cipher.size() || !file.commit()) {
            error = "Cannot atomically persist send recovery";
            return false;
        }
        return true;
    }
    static bool prune(QVector<Record>& records)
    {
        const auto now = QDateTime::currentDateTimeUtc();
        const auto before = records.size();
        records.removeIf(
            [&](const Record& r) { return r.createdAt < now.addDays(-kMaxAgeDays) || r.createdAt > now.addSecs(300); });
        return records.size() != before;
    }
};

SendRecoveryStore::SendRecoveryStore(QString directory, ICredentialStore& credentials)
    : m_impl(std::make_shared<Impl>(std::move(directory), credentials))
{
}
SendRecoveryStore::~SendRecoveryStore()
{
    m_impl->alive = false;
    m_impl->queue.clear();
}
bool SendRecoveryStore::hasDurableState(const QString& accountId) const
{
    const QFileInfo info(m_impl->path(accountId));
    return info.exists() || info.isSymLink();
}

quint64 SendRecoveryStore::generation(const QString& accountId) const
{
    return m_impl->generations.value(accountId);
}

void SendRecoveryStore::load(Binding b, LoadCallback cb)
{
    auto self = m_impl;
    const auto generation = self->generations.value(b.accountId);
    self->enqueue([self, b, generation, cb] {
        self->read(b, generation, [self, b, cb](QByteArray key, QVector<Record> records, QString error) {
            if (error.isEmpty()) {
                bool changed = Impl::prune(records);
                for (auto& r : records)
                    if (r.state == State::Pending && !self->livePending.contains(b.accountId + '/' + r.id)) {
                        r.state = State::Uncertain;
                        changed = true;
                    }
                if (changed)
                    self->write(b, key, records, error);
            }
            if (!key.isEmpty())
                sodium_memzero(key.data(), static_cast<size_t>(key.size()));
            cb(error.isEmpty(), error.isEmpty() ? records : QVector<Record>(), error);
            self->finish();
        });
    });
}

void SendRecoveryStore::stage(Binding b, Record record, Completion cb)
{
    auto self = m_impl;
    const auto generation = self->generations.value(b.accountId);
    const bool deletedAtEnqueue = self->dismissed.contains(b.accountId + '/' + record.id)
        || self->deleting.contains(b.accountId + '/' + record.id);
    self->enqueue([self, b, generation, record, deletedAtEnqueue, cb] {
        self->read(b, generation,
            [self, b, record, deletedAtEnqueue, cb](QByteArray key, QVector<Record> records, QString error) mutable {
                if (error.isEmpty()) {
                    Impl::prune(records);
                    const auto now = QDateTime::currentDateTimeUtc();
                    if (record.id.isEmpty() || record.channelId.isEmpty() || !record.createdAt.isValid()
                        || record.createdAt < now.addDays(-kMaxAgeDays) || record.createdAt > now.addSecs(300)
                        || deletedAtEnqueue || self->dismissed.contains(b.accountId + '/' + record.id)
                        || self->deleting.contains(b.accountId + '/' + record.id))
                        error = "Invalid or dismissed send recovery record";
                    for (const auto& p : record.attachmentPaths)
                        if (p.isEmpty() || !QFileInfo(p).isFile())
                            error = "A send recovery attachment is missing";
                    for (const auto& r : records) {
                        if (r.id != record.id)
                            continue;
                        if (r.state != State::Rejected)
                            error = "This send has uncertain or pending delivery";
                        else if (r.channelId != record.channelId || r.content != record.content
                            || r.original != record.original || r.attachmentPaths != record.attachmentPaths
                            || r.replyTo != record.replyTo || r.action != record.action)
                            error = "Send recovery retry context changed";
                        else
                            record.createdAt = r.createdAt;
                    }
                    if (error.isEmpty()) {
                        records.removeIf([&](const Record& r) { return r.id == record.id; });
                        record.state = State::Pending;
                        records.append(record);
                        if (self->write(b, key, records, error))
                            self->livePending.insert(b.accountId + '/' + record.id);
                    }
                }
                if (!key.isEmpty())
                    sodium_memzero(key.data(), static_cast<size_t>(key.size()));
                if (cb)
                    cb(error.isEmpty(), error);
                self->finish();
            });
    });
}

void SendRecoveryStore::reject(Binding b, QString id, Completion cb, std::optional<quint64> originGeneration)
{
    auto self = m_impl;
    if (originGeneration && *originGeneration != self->generations.value(b.accountId)) {
        if (cb)
            cb(false, QStringLiteral("Send recovery account generation changed"));
        return;
    }
    const auto generation = originGeneration.value_or(self->generations.value(b.accountId));
    self->enqueue([self, b, generation, id, cb] {
        self->read(
            b, generation,
            [self, b, id, cb](QByteArray key, QVector<Record> records, QString error) {
                if (error.isEmpty()) {
                    bool found = false;
                    for (auto& r : records)
                        if (r.id == id && r.state == State::Pending) {
                            r.state = State::Rejected;
                            found = true;
                        }
                    if (!found)
                        error = "No pending send matches this rejection";
                    else {
                        if (self->write(b, key, records, error))
                            self->livePending.remove(b.accountId + '/' + id);
                    }
                }
                if (!key.isEmpty())
                    sodium_memzero(key.data(), static_cast<size_t>(key.size()));
                if (cb)
                    cb(error.isEmpty(), error);
                self->finish();
            },
            false);
    });
}

void SendRecoveryStore::uncertain(Binding b, QString id, Completion cb, std::optional<quint64> originGeneration)
{
    auto self = m_impl;
    if (originGeneration && *originGeneration != self->generations.value(b.accountId)) {
        if (cb)
            cb(false, QStringLiteral("Send recovery account generation changed"));
        return;
    }
    const auto generation = originGeneration.value_or(self->generations.value(b.accountId));
    self->livePending.remove(b.accountId + '/' + id);
    self->enqueue([self, b, generation, id, cb] {
        self->read(
            b, generation,
            [self, b, id, cb](QByteArray key, QVector<Record> records, QString error) {
                if (error.isEmpty()) {
                    bool changed = false;
                    for (auto& r : records)
                        if (r.id == id && r.state == State::Pending) {
                            r.state = State::Uncertain;
                            changed = true;
                        }
                    if (changed)
                        self->write(b, key, records, error);
                }
                if (!key.isEmpty())
                    sodium_memzero(key.data(), static_cast<size_t>(key.size()));
                if (cb)
                    cb(error.isEmpty(), error);
                self->finish();
            },
            false);
    });
}

void SendRecoveryStore::acknowledge(Binding b, QString id, Completion cb, std::optional<quint64> originGeneration)
{
    eraseRecord(std::move(b), std::move(id), std::move(cb), false, originGeneration);
}

void SendRecoveryStore::erase(Binding b, QString id, Completion cb, std::optional<quint64> originGeneration)
{
    eraseRecord(std::move(b), std::move(id), std::move(cb), true, originGeneration);
}

void SendRecoveryStore::eraseRecord(
    Binding b, QString id, Completion cb, bool rememberDismissal, std::optional<quint64> originGeneration)
{
    auto self = m_impl;
    if (originGeneration && *originGeneration != self->generations.value(b.accountId)) {
        if (cb)
            cb(false, QStringLiteral("Send recovery account generation changed"));
        return;
    }
    if (rememberDismissal)
        self->dismissed.insert(b.accountId + '/' + id);
    self->deleting.insert(b.accountId + '/' + id);
    self->livePending.remove(b.accountId + '/' + id);
    const auto generation = originGeneration.value_or(self->generations.value(b.accountId));
    self->enqueue([self, b, generation, id, cb] {
        self->read(
            b, generation,
            [self, b, id, cb](QByteArray key, QVector<Record> records, QString error) {
                if (error.isEmpty()) {
                    const auto before = records.size();
                    records.removeIf([&](const Record& r) { return r.id == id; });
                    if (records.size() != before)
                        self->write(b, key, records, error);
                }
                if (!key.isEmpty())
                    sodium_memzero(key.data(), static_cast<size_t>(key.size()));
                if (cb)
                    cb(error.isEmpty(), error);
                self->deleting.remove(b.accountId + '/' + id);
                self->finish();
            },
            false);
    });
}

void SendRecoveryStore::purge(QString account, Completion cb)
{
    auto self = m_impl;
    self->retired.insert(account);
    ++self->generations[account];
    self->enqueue([self, account, cb] {
        if (account.isEmpty() || self->credentials.backendName() != "keyring") {
            if (cb)
                cb(false, "Secure send recovery purge is unavailable");
            self->finish();
            return;
        }
        // Removing ciphertext first means a failed key deletion cannot expose
        // old sends on next launch. A file deletion failure leaves the key intact
        // and reports failure, so lifecycle callers must not claim purge success.
        if ((QFileInfo::exists(self->path(account)) || QFileInfo(self->path(account)).isSymLink())
            && !QFile::remove(self->path(account))) {
            if (cb)
                cb(false, "Cannot delete send recovery storage");
            self->finish();
            return;
        }
        self->credentials.remove(self->keyName(account), [self, account, cb](bool ok, const QString&) {
            if (!self->alive)
                return;
            if (ok) {
                self->retired.remove(account);
                const auto prefix = account + '/';
                self->livePending.removeIf([&](const QString& id) { return id.startsWith(prefix); });
            }
            if (cb)
                cb(ok, ok ? QString() : QStringLiteral("Cannot delete the secure send recovery key"));
            self->finish();
        });
    });
}

} // namespace omachat::daemon
