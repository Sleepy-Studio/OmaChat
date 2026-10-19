#pragma once

#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryFile>
#include <QUuid>

#include <QVariant>

#include <fcntl.h>
#include <optional>
#include <unistd.h>

namespace omachat::server::storage {

// Must run outside a transaction. VACUUM snapshots committed WAL data as well
// as the main database. The precreated sibling is private and never overwritten.
inline std::optional<QString> migrationBackup(QSqlDatabase source, int version, QString* error)
{
    const auto fail = [error](const char* reason) -> std::optional<QString> {
        if (error)
            *error = QString::fromLatin1(reason);
        return std::nullopt;
    };
    const QFileInfo database(source.databaseName());
    if (!database.isFile())
        return fail("pre-migration backup requires a regular database file");
    QTemporaryFile backup(
        database.absoluteFilePath() + QStringLiteral(".pre-migration-v%1-XXXXXX.sqlite").arg(version));
    if (!backup.open() || !backup.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return fail("could not create a private pre-migration backup; migration aborted");
    const auto path = backup.fileName();
    {
        QSqlQuery vacuum(source);
        if (!vacuum.prepare(QStringLiteral("VACUUM main INTO ?")))
            return fail("could not prepare the pre-migration backup; migration aborted");
        vacuum.addBindValue(path);
        if (!vacuum.exec())
            return fail("could not snapshot the pre-migration database; migration aborted");
    }
    const auto connectionName = QStringLiteral("migration-backup-") + QUuid::createUuid().toString();
    const auto cleanup = qScopeGuard([&] { QSqlDatabase::removeDatabase(connectionName); });
    {
        auto verifier = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        verifier.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        verifier.setDatabaseName(path);
        if (!verifier.open())
            return fail("could not independently open the pre-migration backup; migration aborted");
        QSqlQuery check(verifier);
        if (!check.exec(QStringLiteral("PRAGMA integrity_check")) || !check.next()
            || check.value(0).toString() != QStringLiteral("ok") || check.next())
            return fail("pre-migration backup integrity verification failed; migration aborted");
        check.finish();
        if (!check.exec(QStringLiteral("PRAGMA user_version")) || !check.next() || check.value(0).toInt() != version)
            return fail("pre-migration backup schema verification failed; migration aborted");
        check.finish();
        verifier.close();
    }
    // VACUUM INTO does not itself guarantee target durability on older SQLite.
    if (::fsync(backup.handle()) != 0)
        return fail("could not synchronize the pre-migration backup; migration aborted");
    const auto parent = QFile::encodeName(database.absolutePath());
    const int directory = ::open(parent.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0)
        return fail("could not open the pre-migration backup directory for synchronization; migration aborted");
    const auto closeDirectory = qScopeGuard([&] { ::close(directory); });
    if (::fsync(directory) != 0)
        return fail("could not synchronize the pre-migration backup directory; migration aborted");
    backup.setAutoRemove(false);
    return path;
}

} // namespace omachat::server::storage
