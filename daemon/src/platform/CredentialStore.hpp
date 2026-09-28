#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <map>
#include <memory>

namespace omachat::daemon {

// Boundary to the system secret store. Refresh tokens never touch disk in
// OmaChat's own files, logs or IPC.
class ICredentialStore {
public:
    virtual ~ICredentialStore() = default;

    using ReadCallback = std::function<void(bool ok, const QString& secret, const QString& error)>;
    using WriteCallback = std::function<void(bool ok, const QString& error)>;

    virtual void read(const QString& key, ReadCallback cb) = 0;
    virtual void write(const QString& key, const QString& secret, WriteCallback cb) = 0;
    virtual void remove(const QString& key, WriteCallback cb) = 0;
    virtual QString backendName() const = 0;
};

// Secret Service / KWallet via QtKeychain (asynchronous, never blocks the loop).
std::unique_ptr<ICredentialStore> makeKeychainStore(QObject* parent);

// Process-memory only; used when OMACHAT_CREDENTIALS=memory (tests, or when
// no keyring exists). Tokens are lost when the daemon exits.
class MemoryCredentialStore final : public ICredentialStore {
public:
    void read(const QString& key, ReadCallback cb) override;
    void write(const QString& key, const QString& secret, WriteCallback cb) override;
    void remove(const QString& key, WriteCallback cb) override;
    QString backendName() const override { return QStringLiteral("memory"); }

private:
    std::map<QString, QString> m_secrets;
};

} // namespace omachat::daemon
