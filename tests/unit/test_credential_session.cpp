// These unstable APIs are used only to expire this test process's own session.
#define SECRET_API_SUBJECT_TO_CHANGE
#define SECRET_COMPILATION
#include <libsecret/secret-backend.h>
#include <libsecret/secret.h>

#include "omachat/core/Version.hpp"
#include "platform/CredentialStore.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QUuid>
#include <qt6keychain/keychain.h>

#include <gtest/gtest.h>

namespace {
using namespace omachat::daemon;
struct Result {
    bool done = false;
    bool ok = false;
    QString value;
    QString error;
};

bool wait(const std::shared_ptr<Result>& result)
{
    QElapsedTimer timer;
    timer.start();
    while (!result->done && timer.elapsed() < 15000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return result->done;
}

std::shared_ptr<Result> read(ICredentialStore& store, const QString& key)
{
    auto result = std::make_shared<Result>();
    store.read(
        key, [result](bool ok, const QString& value, const QString& error) { *result = {true, ok, value, error}; });
    wait(result);
    return result;
}

std::shared_ptr<Result> write(ICredentialStore& store, const QString& key, const QString& value)
{
    auto result = std::make_shared<Result>();
    store.write(key, value, [result](bool ok, const QString& error) { *result = {true, ok, {}, error}; });
    wait(result);
    return result;
}

bool expireOwnSession()
{
    struct Backend {
        SecretBackend* value = nullptr;
        GError* error = nullptr;
        std::shared_ptr<Result> ready = std::make_shared<Result>();
        ~Backend()
        {
            if (value)
                g_object_unref(value);
            if (error)
                g_error_free(error);
        }
    };
    auto backend = std::make_shared<Backend>();
    auto* context = new std::shared_ptr<Backend>(backend);
    secret_backend_get(
        SECRET_BACKEND_OPEN_SESSION, nullptr,
        [](GObject*, GAsyncResult* result, gpointer data) {
            std::unique_ptr<std::shared_ptr<Backend>> pending(static_cast<std::shared_ptr<Backend>*>(data));
            (*pending)->value = secret_backend_get_finish(result, &(*pending)->error);
            (*pending)->ready->done = true;
        },
        context);
    if (!wait(backend->ready))
        return false;
    bool closed = false;
    if (backend->value && SECRET_IS_SERVICE(backend->value)) {
        auto* service = SECRET_SERVICE(backend->value);
        const char* path = secret_service_get_session_dbus_path(service);
        if (path) {
            auto* reply = g_dbus_connection_call_sync(g_dbus_proxy_get_connection(G_DBUS_PROXY(service)),
                "org.freedesktop.secrets", path, "org.freedesktop.Secret.Session", "Close", nullptr, nullptr,
                G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, &backend->error);
            closed = reply != nullptr;
            if (reply)
                g_variant_unref(reply);
        }
    }
    return closed;
}

TEST(CredentialSessionRealKeyring, ExpiredSessionReadAndWriteRecoverWithoutDaemonRestart)
{
    if (qEnvironmentVariableIntValue("OMACHAT_TEST_REAL_KEYRING") != 1)
        GTEST_SKIP() << "Set OMACHAT_TEST_REAL_KEYRING=1 in an unlocked desktop session";
    const bool hadBackend = qEnvironmentVariableIsSet("QTKEYCHAIN_BACKEND");
    const QByteArray oldBackend = qgetenv("QTKEYCHAIN_BACKEND");
    qputenv("QTKEYCHAIN_BACKEND", "libsecret");
    QObject owner;
    auto store = makeKeychainStore(&owner);
    const QString key = "test-expired-session-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    struct Cleanup {
        std::function<void()> run;
        ~Cleanup() { run(); }
    } cleanup{[&] {
        secret_service_disconnect();
        auto result = std::make_shared<Result>();
        store->remove(key, [result](bool ok, const QString& error) { *result = {true, ok, {}, error}; });
        EXPECT_TRUE(wait(result));
        EXPECT_TRUE(result->ok) << result->error.toStdString();
        if (hadBackend)
            qputenv("QTKEYCHAIN_BACKEND", oldBackend);
        else
            qunsetenv("QTKEYCHAIN_BACKEND");
    }};
    ASSERT_TRUE(write(*store, key, "synthetic-session-secret")->ok);
    ASSERT_TRUE(expireOwnSession());
    // A raw QtKeychain job establishes that the exact production error exists.
    QKeychain::ReadPasswordJob raw(QString::fromLatin1(omachat::kAppId));
    raw.setAutoDelete(false);
    raw.setInsecureFallback(false);
    raw.setKey(key);
    auto rawDone = std::make_shared<Result>();
    QObject::connect(&raw, &QKeychain::Job::finished, &owner, [rawDone] { rawDone->done = true; });
    raw.start();
    ASSERT_TRUE(wait(rawDone));
    ASSERT_EQ(raw.error(), QKeychain::OtherError);
    ASSERT_EQ(raw.errorString(), "The session does not exist");
    auto result = read(*store, key);
    ASSERT_TRUE(result->done);
    ASSERT_TRUE(result->ok) << result->error.toStdString();
    EXPECT_EQ(result->value, "synthetic-session-secret");
    ASSERT_TRUE(expireOwnSession());
    auto updated = write(*store, key, "synthetic-updated-secret");
    ASSERT_TRUE(updated->done);
    ASSERT_TRUE(updated->ok) << updated->error.toStdString();
    EXPECT_EQ(read(*store, key)->value, "synthetic-updated-secret");
}
} // namespace
