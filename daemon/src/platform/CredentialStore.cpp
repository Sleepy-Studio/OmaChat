#include "platform/CredentialStore.hpp"

#include "omachat/core/Version.hpp"

#include <qt6keychain/keychain.h>

namespace omachat::daemon {

namespace {

class KeychainStore final : public ICredentialStore {
public:
    explicit KeychainStore(QObject* parent)
        : m_parent(parent)
    {
    }

    void read(const QString& key, ReadCallback cb) override
    {
        auto* job = new QKeychain::ReadPasswordJob(QString::fromLatin1(kAppId), m_parent);
        job->setAutoDelete(true);
        job->setKey(key);
        QObject::connect(job, &QKeychain::Job::finished, m_parent, [cb = std::move(cb)](QKeychain::Job* j) {
            auto* rj = static_cast<QKeychain::ReadPasswordJob*>(j);
            if (rj->error() == QKeychain::NoError)
                cb(true, rj->textData(), QString());
            else if (rj->error() == QKeychain::EntryNotFound)
                cb(true, QString(), QString());
            else
                cb(false, QString(), rj->errorString());
        });
        job->start();
    }

    void write(const QString& key, const QString& secret, WriteCallback cb) override
    {
        auto* job = new QKeychain::WritePasswordJob(QString::fromLatin1(kAppId), m_parent);
        job->setAutoDelete(true);
        job->setKey(key);
        job->setTextData(secret);
        QObject::connect(job, &QKeychain::Job::finished, m_parent, [cb = std::move(cb)](QKeychain::Job* j) {
            if (cb)
                cb(j->error() == QKeychain::NoError, j->errorString());
        });
        job->start();
    }

    void remove(const QString& key, WriteCallback cb) override
    {
        auto* job = new QKeychain::DeletePasswordJob(QString::fromLatin1(kAppId), m_parent);
        job->setAutoDelete(true);
        job->setKey(key);
        QObject::connect(job, &QKeychain::Job::finished, m_parent, [cb = std::move(cb)](QKeychain::Job* j) {
            const bool ok = j->error() == QKeychain::NoError || j->error() == QKeychain::EntryNotFound;
            if (cb)
                cb(ok, j->errorString());
        });
        job->start();
    }

    QString backendName() const override { return QStringLiteral("keyring"); }

private:
    QObject* m_parent;
};

} // namespace

std::unique_ptr<ICredentialStore> makeKeychainStore(QObject* parent)
{
    return std::make_unique<KeychainStore>(parent);
}

void MemoryCredentialStore::read(const QString& key, ReadCallback cb)
{
    auto it = m_secrets.find(key);
    cb(true, it == m_secrets.end() ? QString() : it->second, QString());
}

void MemoryCredentialStore::write(const QString& key, const QString& secret, WriteCallback cb)
{
    m_secrets[key] = secret;
    if (cb)
        cb(true, QString());
}

void MemoryCredentialStore::remove(const QString& key, WriteCallback cb)
{
    m_secrets.erase(key);
    if (cb)
        cb(true, QString());
}

} // namespace omachat::daemon
