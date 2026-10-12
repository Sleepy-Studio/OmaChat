#pragma once

#include "platform/CredentialStore.hpp"

#include <QDateTime>
#include <QStringList>
#include <QVector>

#include <memory>
#include <optional>

namespace omachat::daemon {

// Single daemon/event-loop owner. Credentials must outlive this store and its
// outstanding callbacks. Never dispatch a send before stage's successful callback.
class SendRecoveryStore {
public:
    struct Binding {
        QString accountId;
        QString endpoint;
        QString certificatePin;
        QString userId;
        bool operator==(const Binding&) const = default;
    };
    enum class State { Pending, Rejected, Uncertain };
    struct Record {
        QString id;
        QString channelId;
        QString content;
        QString original;
        QStringList attachmentPaths;
        QString replyTo;
        bool action = false;
        QDateTime createdAt;
        State state = State::Pending;
    };
    // Conservative implementation limits, applied to the whole account document.
    static constexpr int kMaxRecords = 20;
    static constexpr int kMaxBytes = 128 * 1024;
    static constexpr int kMaxAgeDays = 30;
    using Completion = ICredentialStore::WriteCallback;
    using LoadCallback = std::function<void(bool, QVector<Record>, const QString&)>;

    SendRecoveryStore(QString directory, ICredentialStore& credentials);
    ~SendRecoveryStore();
    SendRecoveryStore(const SendRecoveryStore&) = delete;
    SendRecoveryStore& operator=(const SendRecoveryStore&) = delete;

    // Restored pending sends become uncertain durably; caller must never retry
    // them automatically. Binding mismatch fails closed and exposes no records.
    void load(Binding binding, LoadCallback callback);
    // New record, or explicit retry of a rejected record. Existing IDs cannot
    // overwrite pending/uncertain records; dismissed IDs cannot be reused.
    void stage(Binding binding, Record record, Completion callback);
    // Only a positively correlated server rejection permits manual retry.
    void reject(
        Binding binding, QString id, Completion callback, std::optional<quint64> originGeneration = std::nullopt);
    void uncertain(
        Binding binding, QString id, Completion callback, std::optional<quint64> originGeneration = std::nullopt);
    // Acknowledgement blocks concurrent writes while deleting, without retaining
    // successful-send IDs for the daemon lifetime. Failure remains visible.
    void acknowledge(
        Binding binding, QString id, Completion callback, std::optional<quint64> originGeneration = std::nullopt);
    // User dismissal retains a process-local tombstone against stale retries.
    void erase(
        Binding binding, QString id, Completion callback, std::optional<quint64> originGeneration = std::nullopt);
    // Account lifecycle boundary: queued writes are invalidated immediately.
    // Failure retires the account until a successful purge retry. Callers must
    // prevent successful logout/removal on failure.
    void purge(QString accountId, Completion callback);
    bool hasDurableState(const QString& accountId) const;
    // Capture at network dispatch; pass back to all cleanup callbacks. A purge
    // invalidates old completions even if the same account signs in again.
    quint64 generation(const QString& accountId) const;

private:
    void eraseRecord(Binding binding, QString id, Completion callback, bool rememberDismissal,
        std::optional<quint64> originGeneration);
    struct Impl;
    std::shared_ptr<Impl> m_impl;
};

} // namespace omachat::daemon
