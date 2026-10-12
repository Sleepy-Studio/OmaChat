#include "storage/SendRecoveryStore.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <gtest/gtest.h>

using namespace omachat::daemon;
namespace {
// Explicit test double, never selected by production configuration.
class SecureFixture : public ICredentialStore {
public:
    QMap<QString, QString> secrets;
    bool failRead = false, failWrite = false, failRemove = false, delay = false;
    std::function<void()> pending;
    QString backendName() const override { return QStringLiteral("keyring"); }
    void read(const QString& key, ReadCallback cb) override
    {
        auto work = [this, key, cb] { cb(!failRead, secrets.value(key), failRead ? "locked" : ""); };
        if (delay)
            pending = work;
        else
            work();
    }
    void write(const QString& key, const QString& value, WriteCallback cb) override
    {
        if (!failWrite)
            secrets[key] = value;
        cb(!failWrite, failWrite ? "locked" : "");
    }
    void remove(const QString& key, WriteCallback cb) override
    {
        if (!failRemove)
            secrets.remove(key);
        cb(!failRemove, failRemove ? "locked" : "");
    }
    void complete()
    {
        delay = false;
        auto work = std::move(pending);
        work();
    }
};
const SendRecoveryStore::Binding binding{"local-account", "https://example.test", "certificate", "user-1"};
SendRecoveryStore::Record record(QString id = "one")
{
    SendRecoveryStore::Record r;
    r.id = id;
    r.channelId = "channel";
    r.content = "private body";
    r.original = "original composer";
    r.createdAt = QDateTime::currentDateTimeUtc();
    return r;
}
bool stage(SendRecoveryStore& store, SendRecoveryStore::Record r)
{
    bool ok = false;
    store.stage(binding, std::move(r), [&](bool result, const QString&) { ok = result; });
    return ok;
}
QVector<SendRecoveryStore::Record> load(SendRecoveryStore& store, bool* success = nullptr)
{
    QVector<SendRecoveryStore::Record> records;
    store.load(binding, [&](bool ok, auto found, const QString&) {
        if (success)
            *success = ok;
        records = found;
    });
    return records;
}
} // namespace

TEST(SendRecovery, RestartUncertaintyAndCiphertextOnly)
{
    QTemporaryDir dir;
    SecureFixture keys;
    {
        SendRecoveryStore store(dir.path(), keys);
        ASSERT_TRUE(stage(store, record()));
    }
    const auto names = QDir(dir.path()).entryList(QDir::Files);
    ASSERT_EQ(names.size(), 1);
    QFile file(dir.filePath(names.front()));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const auto bytes = file.readAll();
    EXPECT_FALSE(bytes.contains("private body"));
    EXPECT_FALSE(bytes.contains("original composer"));
    EXPECT_FALSE(bytes.contains("example.test"));
    EXPECT_FALSE(bytes.contains("local-account"));
    EXPECT_EQ(file.permissions() & (QFile::ReadGroup | QFile::ReadOther | QFile::WriteGroup | QFile::WriteOther), 0);
    SendRecoveryStore restored(dir.path(), keys);
    auto records = load(restored);
    ASSERT_EQ(records.size(), 1);
    EXPECT_EQ(records[0].state, SendRecoveryStore::State::Uncertain);
    EXPECT_EQ(records[0].original, "original composer");
    EXPECT_FALSE(stage(restored, record()));
}

TEST(SendRecovery, RejectionAllowsExplicitRetryAcknowledgementDeletes)
{
    QTemporaryDir dir;
    SecureFixture keys;
    SendRecoveryStore store(dir.path(), keys);
    ASSERT_TRUE(stage(store, record()));
    bool rejected = false;
    store.reject(binding, "one", [&](bool ok, const QString&) { rejected = ok; });
    ASSERT_TRUE(rejected);
    SendRecoveryStore restored(dir.path(), keys);
    ASSERT_EQ(load(restored)[0].state, SendRecoveryStore::State::Rejected);
    ASSERT_TRUE(stage(restored, record()));
    bool erased = false;
    restored.erase(binding, "one", [&](bool ok, const QString&) { erased = ok; });
    ASSERT_TRUE(erased);
    EXPECT_TRUE(load(restored).isEmpty());
    EXPECT_FALSE(stage(restored, record()));
}

TEST(SendRecovery, BindingTamperingMissingKeyAndLockedKeyringFailClosed)
{
    QTemporaryDir dir;
    SecureFixture keys;
    SendRecoveryStore store(dir.path(), keys);
    ASSERT_TRUE(stage(store, record()));
    for (int field = 0; field < 4; ++field) {
        auto other = binding;
        if (field == 0)
            other.accountId += "different";
        if (field == 1)
            other.endpoint += "different";
        if (field == 2)
            other.certificatePin += "different";
        if (field == 3)
            other.userId += "different";
        bool ok = true;
        store.load(other, [&](bool result, auto records, const QString&) {
            ok = result;
            EXPECT_TRUE(records.isEmpty());
        });
        EXPECT_EQ(ok, field == 0); // Different account has independent empty state.
    }
    keys.failRead = true;
    bool ok = true;
    EXPECT_TRUE(load(store, &ok).isEmpty());
    EXPECT_FALSE(ok);
    keys.failRead = false;
    const auto name = QDir(dir.path()).entryList(QDir::Files).front();
    QFile file(dir.filePath(name));
    ASSERT_TRUE(file.open(QIODevice::ReadWrite));
    auto cipher = file.readAll();
    cipher[cipher.size() - 1] ^= 1;
    file.seek(0);
    file.write(cipher);
    file.close();
    EXPECT_TRUE(load(store, &ok).isEmpty());
    EXPECT_FALSE(ok);
    keys.secrets.clear();
    EXPECT_TRUE(load(store, &ok).isEmpty());
    EXPECT_FALSE(ok);
}

TEST(SendRecovery, SecureBackendAndKeyPersistenceRequired)
{
    QTemporaryDir dir;
    MemoryCredentialStore memory;
    SendRecoveryStore insecure(dir.path(), memory);
    EXPECT_FALSE(stage(insecure, record()));
    EXPECT_TRUE(QDir(dir.path()).entryList(QDir::Files).isEmpty());
    SecureFixture keys;
    keys.failWrite = true;
    SendRecoveryStore unavailable(dir.path(), keys);
    EXPECT_FALSE(stage(unavailable, record()));
    EXPECT_TRUE(QDir(dir.path()).entryList(QDir::Files).isEmpty());
}

TEST(SendRecovery, LimitsRetentionAndMissingAttachments)
{
    QTemporaryDir dir;
    SecureFixture keys;
    SendRecoveryStore store(dir.path(), keys);
    auto missing = record();
    missing.attachmentPaths = {dir.filePath("absent")};
    EXPECT_FALSE(stage(store, missing));
    auto old = record();
    old.createdAt = old.createdAt.addDays(-31);
    EXPECT_FALSE(stage(store, old));
    auto big = record();
    big.content = QString(SendRecoveryStore::kMaxBytes, 'x');
    EXPECT_FALSE(stage(store, big));
    for (int i = 0; i < SendRecoveryStore::kMaxRecords; ++i)
        ASSERT_TRUE(stage(store, record(QString::number(i))));
    EXPECT_FALSE(stage(store, record("overflow")));
    EXPECT_EQ(load(store).size(), SendRecoveryStore::kMaxRecords);
}

TEST(SendRecovery, DelayedStageCannotResurrectDismissedOrPurgedRecords)
{
    QTemporaryDir dir;
    SecureFixture keys;
    SendRecoveryStore store(dir.path(), keys);
    keys.delay = true;
    bool staged = true, erased = false;
    store.stage(binding, record(), [&](bool ok, const QString&) { staged = ok; });
    store.erase(binding, "one", [&](bool ok, const QString&) { erased = ok; });
    keys.complete();
    EXPECT_FALSE(staged);
    EXPECT_TRUE(erased);
    EXPECT_TRUE(load(store).isEmpty());
    keys.delay = true;
    staged = true;
    store.stage(binding, record("two"), [&](bool ok, const QString&) { staged = ok; });
    bool purged = false;
    store.purge(binding.accountId, [&](bool ok, const QString&) { purged = ok; });
    keys.complete();
    EXPECT_FALSE(staged);
    EXPECT_TRUE(purged);
    EXPECT_TRUE(QDir(dir.path()).entryList(QDir::Files).isEmpty());
    EXPECT_TRUE(keys.secrets.isEmpty());
    EXPECT_TRUE(stage(store, record("three"))); // A new login generation may send.
}

TEST(SendRecovery, PurgeFailureIsVisibleAndRetiresAccount)
{
    QTemporaryDir dir;
    SecureFixture keys;
    SendRecoveryStore store(dir.path(), keys);
    ASSERT_TRUE(stage(store, record()));
    keys.failRemove = true;
    bool purged = true;
    store.purge(binding.accountId, [&](bool ok, const QString&) { purged = ok; });
    EXPECT_FALSE(purged);
    EXPECT_FALSE(stage(store, record("two")));
    EXPECT_TRUE(QDir(dir.path()).entryList(QDir::Files).isEmpty());
    EXPECT_FALSE(keys.secrets.isEmpty());
}

TEST(SendRecovery, LivePendingListDoesNotTurnIntoRetryAndRetryContextIsImmutable)
{
    QTemporaryDir dir;
    SecureFixture keys;
    SendRecoveryStore store(dir.path(), keys);
    ASSERT_TRUE(stage(store, record()));
    ASSERT_EQ(load(store)[0].state, SendRecoveryStore::State::Pending);
    bool ok = false;
    store.reject(binding, "one", [&](bool result, const QString&) { ok = result; });
    ASSERT_TRUE(ok);
    auto changed = record();
    changed.channelId = "other-channel";
    EXPECT_FALSE(stage(store, changed));
    changed = record();
    changed.content += "edited";
    EXPECT_FALSE(stage(store, changed));
    ASSERT_TRUE(stage(store, record()));
    store.uncertain(binding, "one", [&](bool result, const QString&) { ok = result; });
    ASSERT_TRUE(ok);
    EXPECT_EQ(load(store)[0].state, SendRecoveryStore::State::Uncertain);
    EXPECT_FALSE(stage(store, record()));
}

TEST(SendRecovery, SymlinksAndWorldReadableStorageFailClosed)
{
    QTemporaryDir dir;
    SecureFixture keys;
    SendRecoveryStore store(dir.path(), keys);
    ASSERT_TRUE(stage(store, record()));
    const auto file = dir.filePath(QDir(dir.path()).entryList(QDir::Files).front());
    ASSERT_TRUE(QFile::setPermissions(file, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadOther));
    bool ok = true;
    EXPECT_TRUE(load(store, &ok).isEmpty());
    EXPECT_FALSE(ok);
    ASSERT_TRUE(QFile::setPermissions(file, QFile::ReadOwner | QFile::WriteOwner));
    QTemporaryDir other;
    const auto copy = other.filePath("foreign");
    ASSERT_TRUE(QFile::rename(file, copy));
    ASSERT_TRUE(QFile::link(copy, file));
    EXPECT_TRUE(load(store, &ok).isEmpty());
    EXPECT_FALSE(ok);
    ASSERT_TRUE(QFile::remove(file));
    ASSERT_TRUE(QFile::link(dir.path(), other.filePath("directory-link")));
    SendRecoveryStore linked(other.filePath("directory-link"), keys);
    EXPECT_FALSE(stage(linked, record("new")));
}

TEST(SendRecoveryRealKeyring, OptInRoundTripAndCleanup)
{
    if (qEnvironmentVariableIntValue("OMACHAT_TEST_REAL_KEYRING") != 1)
        GTEST_SKIP() << "Set OMACHAT_TEST_REAL_KEYRING=1 in an unlocked desktop session";
    ASSERT_NE(QCoreApplication::instance(), nullptr);
    QObject owner;
    auto keys = makeKeychainStore(&owner);
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto context = binding;
    context.accountId = "test-send-recovery-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    SendRecoveryStore store(dir.path(), *keys);
    struct Result {
        bool done = false;
        bool ok = false;
        QVector<SendRecoveryStore::Record> records;
    };
    auto wait = [](const std::shared_ptr<Result>& result) {
        QElapsedTimer timer;
        timer.start();
        while (!result->done && timer.elapsed() < 15000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(1);
        }
        return result->done;
    };
    struct Cleanup {
        std::function<void()> run;
        ~Cleanup() { run(); }
    } cleanup{[&] {
        auto result = std::make_shared<Result>();
        store.purge(context.accountId, [result](bool ok, const QString&) {
            result->ok = ok;
            result->done = true;
        });
        EXPECT_TRUE(wait(result)) << "Real-keyring cleanup timed out";
        EXPECT_TRUE(result->ok) << "Real-keyring recovery key cleanup failed";
    }};
    auto staged = std::make_shared<Result>();
    auto sample = record();
    sample.content = "Harmless real-keyring recovery fixture";
    sample.original = sample.content;
    store.stage(context, sample, [staged](bool ok, const QString&) {
        staged->ok = ok;
        staged->done = true;
    });
    ASSERT_TRUE(wait(staged));
    ASSERT_TRUE(staged->ok) << "Actual secure keyring read/write unavailable";
    SendRecoveryStore restored(dir.path(), *keys);
    auto loaded = std::make_shared<Result>();
    restored.load(context, [loaded](bool ok, auto records, const QString&) {
        loaded->ok = ok;
        loaded->records = std::move(records);
        loaded->done = true;
    });
    ASSERT_TRUE(wait(loaded));
    ASSERT_TRUE(loaded->ok);
    ASSERT_EQ(loaded->records.size(), 1);
    EXPECT_EQ(loaded->records[0].state, SendRecoveryStore::State::Uncertain);
    auto erased = std::make_shared<Result>();
    restored.erase(context, sample.id, [erased](bool ok, const QString&) {
        erased->ok = ok;
        erased->done = true;
    });
    ASSERT_TRUE(wait(erased));
    EXPECT_TRUE(erased->ok);
}

TEST(SendRecovery, AcknowledgedIdsDoNotAccumulateButConcurrentRestagingIsBlocked)
{
    QTemporaryDir dir;
    SecureFixture keys;
    SendRecoveryStore store(dir.path(), keys);
    ASSERT_TRUE(stage(store, record()));
    keys.delay = true;
    bool acknowledged = false, restaged = true;
    store.acknowledge(binding, "one", [&](bool ok, const QString&) { acknowledged = ok; });
    store.stage(binding, record(), [&](bool ok, const QString&) { restaged = ok; });
    keys.complete();
    EXPECT_TRUE(acknowledged);
    EXPECT_FALSE(restaged);
    EXPECT_TRUE(load(store).isEmpty());
    // A genuinely new operation may reuse an acknowledged identifier; only
    // dismissals retain a process-local tombstone. The GUI uses fresh UUIDs.
    EXPECT_TRUE(stage(store, record()));
}

TEST(SendRecovery, LateCleanupAfterPurgeNeverRecreatesStorageOrTouchesNewLogin)
{
    QTemporaryDir dir;
    SecureFixture keys;
    SendRecoveryStore store(dir.path(), keys);
    ASSERT_TRUE(stage(store, record()));
    const auto oldGeneration = store.generation(binding.accountId);
    bool purged = false;
    store.purge(binding.accountId, [&](bool ok, const QString&) { purged = ok; });
    ASSERT_TRUE(purged);
    ASSERT_FALSE(store.hasDurableState(binding.accountId));
    ASSERT_TRUE(keys.secrets.isEmpty());
    // Legacy/no-origin cleanup cannot initialize missing keys or ciphertext.
    store.acknowledge(binding, "one", {});
    store.uncertain(binding, "one", {});
    store.reject(binding, "one", {});
    store.erase(binding, "unknown", {});
    EXPECT_FALSE(store.hasDurableState(binding.accountId));
    EXPECT_TRUE(keys.secrets.isEmpty());
    ASSERT_TRUE(stage(store, record())); // New session may reuse the same ID.
    for (int kind = 0; kind < 4; ++kind) {
        bool succeeded = true;
        auto completion = [&](bool ok, const QString&) { succeeded = ok; };
        if (kind == 0)
            store.acknowledge(binding, "one", completion, oldGeneration);
        if (kind == 1)
            store.uncertain(binding, "one", completion, oldGeneration);
        if (kind == 2)
            store.reject(binding, "one", completion, oldGeneration);
        if (kind == 3)
            store.erase(binding, "one", completion, oldGeneration);
        EXPECT_FALSE(succeeded);
        auto records = load(store);
        ASSERT_EQ(records.size(), 1);
        EXPECT_EQ(records[0].state, SendRecoveryStore::State::Pending);
    }
}
