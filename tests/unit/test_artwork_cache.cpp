#include "platform/ArtworkCache.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using omachat::client::ArtworkCache;

namespace {
QSet<QString> reconcile(ArtworkCache& cache, const QString& identity, const QSet<QString>& authorized,
    const QSet<QString>& active, bool known = true)
{
    const auto removed = cache.reconcile(identity, authorized, active, known);
    auto cleanup = ArtworkCache::scanCleanup(cache.prepareCleanup());
    while (cache.finishCleanup(cleanup) == ArtworkCache::CleanupResult::More) { }
    return removed;
}
QString put(ArtworkCache& cache, const QString& id, Qt::GlobalColor color = Qt::red)
{
    const QString staging = cache.stagingPath(id);
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(color);
    if (staging.isEmpty() || !image.save(staging, "PNG"))
        return {};
    return cache.store(id, staging, cache.generation());
}
} // namespace

TEST(ArtworkCache, OfflineRestartUsesOnlyCurrentAuthorizedScope)
{
    QTemporaryDir dir;
    ArtworkCache first(dir.path());
    reconcile(first, "account/host/443/user/1/pin-a", {"7"}, {"7"});
    const auto path = put(first, "7");
    ASSERT_FALSE(path.isEmpty());
    ArtworkCache restarted(dir.path());
    EXPECT_TRUE(restarted.lookup("7").isEmpty());
    reconcile(restarted, "account/host/443/user/1/pin-a", {"7"}, {"7"});
    EXPECT_EQ(restarted.lookup("7"), path);
    EXPECT_FALSE(QFile::permissions(path) & (QFileDevice::ReadGroup | QFileDevice::ReadOther));
}

TEST(ArtworkCache, AccountEndpointAndAuthenticationIdentityNeverShareFiles)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path());
    reconcile(cache, "account/host/443/user/1/pin-a", {"7"}, {});
    const auto original = put(cache, "7");
    ASSERT_FALSE(original.isEmpty());
    for (const auto* identity : {"other/host/443/user/1", "account/elsewhere/443/user/1", "account/host/8443/user/1",
             "account/host/443/user/2", "account/host/443/user/1/pin-b"}) {
        const auto generation = cache.generation();
        EXPECT_TRUE(reconcile(cache, identity, {"7"}, {}).contains("7"));
        EXPECT_GT(cache.generation(), generation);
        EXPECT_TRUE(cache.lookup("7").isEmpty());
        const auto path = put(cache, "7", Qt::blue);
        ASSERT_FALSE(path.isEmpty());
        EXPECT_NE(path, original);
    }
}

TEST(ArtworkCache, ReplacementAndLostAccessDeleteObsoleteFilesEvenWhenPinned)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path());
    reconcile(cache, "scope", {"old"}, {"old"});
    const auto old = put(cache, "old");
    ASSERT_FALSE(old.isEmpty());
    EXPECT_TRUE(reconcile(cache, "scope", {"new"}, {"new"}).contains("old"));
    EXPECT_FALSE(QFile::exists(old));
    const auto replacement = put(cache, "new");
    ASSERT_FALSE(replacement.isEmpty());
    EXPECT_TRUE(reconcile(cache, "scope", {}, {}).contains("new"));
    EXPECT_FALSE(QFile::exists(replacement));
    EXPECT_TRUE(cache.lookup("new").isEmpty());
    EXPECT_TRUE(cache.stagingPath("new").isEmpty());
}

TEST(ArtworkCache, RestartRemovesPreviouslyAuthorizedButNowRevokedFiles)
{
    QTemporaryDir dir;
    QString path;
    {
        ArtworkCache cache(dir.path());
        reconcile(cache, "scope", {"revoked"}, {});
        path = put(cache, "revoked");
        ASSERT_FALSE(path.isEmpty());
    }
    ArtworkCache cache(dir.path());
    reconcile(cache, "scope", {}, {});
    EXPECT_FALSE(QFile::exists(path));
}

TEST(ArtworkCache, CorruptEntriesAndDownloadsAreRejectedAndCanRecover)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path());
    reconcile(cache, "scope", {"7"}, {"7"});
    const auto path = put(cache, "7");
    ASSERT_FALSE(path.isEmpty());
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("not an image");
    file.close();
    EXPECT_TRUE(cache.lookup("7").isEmpty());
    EXPECT_FALSE(QFile::exists(path));
    const auto stage = cache.stagingPath("7");
    QFile bad(stage);
    ASSERT_TRUE(bad.open(QIODevice::WriteOnly));
    bad.write("bad");
    bad.close();
    EXPECT_TRUE(cache.store("7", stage, cache.generation()).isEmpty());
    EXPECT_FALSE(QFile::exists(stage));
    EXPECT_FALSE(put(cache, "7").isEmpty());
}

TEST(ArtworkCache, GlobalCountBudgetEvictsOtherScopesAndPreservesActiveFiles)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path(), {1024 * 1024, 2, 1024 * 1024});
    reconcile(cache, "old-account", {"old"}, {});
    const auto old = put(cache, "old");
    reconcile(cache, "current", {"active", "second", "third"}, {"active"});
    const auto active = put(cache, "active");
    ASSERT_FALSE(active.isEmpty());
    ASSERT_FALSE(put(cache, "second").isEmpty());
    EXPECT_FALSE(QFile::exists(old));
    ASSERT_FALSE(put(cache, "third").isEmpty());
    EXPECT_TRUE(QFile::exists(active));
    EXPECT_EQ(cache.lookup("active"), active);
}

TEST(ArtworkCache, FullPinnedBudgetRejectsNewEntryWithoutDeletingActiveFiles)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path(), {1024 * 1024, 1, 1024 * 1024});
    reconcile(cache, "scope", {"active", "other"}, {"active"});
    const auto active = put(cache, "active");
    ASSERT_FALSE(active.isEmpty());
    EXPECT_TRUE(put(cache, "other").isEmpty());
    EXPECT_TRUE(QFile::exists(active));
}

TEST(ArtworkCache, ByteAndPerImageBudgetsRejectOversizeFiles)
{
    QTemporaryDir dir;
    ArtworkCache tinyBudget(dir.path(), {1, 10, 1024});
    reconcile(tinyBudget, "scope", {"7"}, {});
    EXPECT_TRUE(put(tinyBudget, "7").isEmpty());
    ArtworkCache tinyImage(dir.path(), {1024, 10, 1});
    reconcile(tinyImage, "scope", {"7"}, {});
    EXPECT_TRUE(put(tinyImage, "7").isEmpty());
}

TEST(ArtworkCache, InFlightCompletionCannotCrossAccountsOrRestoreRevokedAccess)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path());
    reconcile(cache, "one", {"7"}, {});
    const auto stage = cache.stagingPath("7");
    const auto generation = cache.generation();
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(Qt::green);
    ASSERT_TRUE(image.save(stage, "PNG"));
    reconcile(cache, "two", {"7"}, {});
    EXPECT_TRUE(cache.store("7", stage, generation).isEmpty());
    EXPECT_FALSE(QFile::exists(stage));
    const auto revoked = cache.stagingPath("7");
    ASSERT_TRUE(image.save(revoked, "PNG"));
    reconcile(cache, "two", {}, {});
    EXPECT_TRUE(cache.store("7", revoked, cache.generation()).isEmpty());
    EXPECT_FALSE(QFile::exists(revoked));
}

TEST(ArtworkCache, ImmutableActiveEntryIsNeverReplacedByDuplicateDownload)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path());
    reconcile(cache, "scope", {"7"}, {"7"});
    const auto first = put(cache, "7", Qt::red);
    ASSERT_FALSE(first.isEmpty());
    EXPECT_EQ(put(cache, "7", Qt::blue), first);
    EXPECT_EQ(QImage(first).pixelColor(0, 0), QColor(Qt::red));
}

TEST(ArtworkCache, UnknownStartupAuthorizationPreservesDiskUntilSnapshotArrives)
{
    QTemporaryDir dir;
    ArtworkCache first(dir.path());
    reconcile(first, "scope", {"7"}, {});
    const auto path = put(first, "7");
    ASSERT_FALSE(path.isEmpty());
    ArtworkCache restarted(dir.path());
    reconcile(restarted, "scope", {}, {}, false);
    EXPECT_TRUE(QFile::exists(path));
    EXPECT_TRUE(restarted.lookup("7").isEmpty());
    reconcile(restarted, "scope", {"7"}, {"7"});
    EXPECT_EQ(restarted.lookup("7"), path);
    ArtworkCache revoked(dir.path());
    reconcile(revoked, "scope", {}, {}, false);
    EXPECT_TRUE(QFile::exists(path));
    reconcile(revoked, "scope", {}, {}, true);
    EXPECT_FALSE(QFile::exists(path));
}

TEST(ArtworkCache, ConcurrentStagingIsBoundedAndFailuresReleaseTheirSlots)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path());
    reconcile(cache, "scope", {"7"}, {});
    QStringList stages;
    for (int i = 0; i < 16; ++i) {
        stages.append(cache.stagingPath("7"));
        ASSERT_FALSE(stages.last().isEmpty());
    }
    EXPECT_TRUE(cache.stagingFull());
    EXPECT_TRUE(cache.stagingPath("7").isEmpty());
    cache.discard(stages.first());
    EXPECT_FALSE(cache.stagingFull());
    EXPECT_FALSE(cache.stagingPath("7").isEmpty());
}

TEST(ArtworkCache, SubstitutedScopeDirectoryCannotReadOrDeleteOutsideCache)
{
    QTemporaryDir dir;
    QTemporaryDir outside;
    ArtworkCache cache(dir.path());
    reconcile(cache, "scope", {"7"}, {});
    const auto path = put(cache, "7");
    ASSERT_FALSE(path.isEmpty());
    const QFileInfo entry(path);
    const auto external = outside.filePath(entry.fileName());
    ASSERT_TRUE(QFile::copy(path, external));
    ASSERT_TRUE(QFile::remove(path));
    ASSERT_TRUE(QDir().rmdir(entry.absolutePath()));
    ASSERT_TRUE(QFile::link(outside.path(), entry.absolutePath()));
    EXPECT_TRUE(cache.lookup("7").isEmpty());
    cache.invalidate("7");
    EXPECT_TRUE(QFile::exists(external));
    EXPECT_TRUE(put(cache, "7").isEmpty());
    EXPECT_TRUE(QFile::exists(external));
}

TEST(ArtworkCache, SubstitutedRootCannotEvictExternalEntriesOrExpiredStaging)
{
    QTemporaryDir dir;
    QTemporaryDir outside;
    const auto root = dir.filePath("cache");
    ArtworkCache cache(root, {1, 0, 1024});
    reconcile(cache, "scope", {"7"}, {});
    ASSERT_TRUE(QDir().mkpath(outside.filePath("other-scope")));
    ASSERT_TRUE(QDir().mkpath(outside.filePath("staging")));
    const QStringList files{outside.filePath("other-scope/entry.image"), outside.filePath("staging/expired.image"),
        outside.filePath("staging/expired.image.part")};
    for (const auto& path : files) {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write("external"), 8);
        ASSERT_TRUE(file.setFileTime(QDateTime::currentDateTimeUtc().addDays(-2), QFileDevice::FileModificationTime));
    }
    ASSERT_TRUE(QFile::setPermissions(outside.path(),
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner | QFileDevice::ReadGroup
            | QFileDevice::ExeGroup));
    const auto permissions = QFile::permissions(outside.path());
    ASSERT_TRUE(QDir(root).removeRecursively());
    ASSERT_TRUE(QFile::link(outside.path(), root));
    reconcile(cache, "scope", {"8"}, {});
    EXPECT_TRUE(cache.lookup("8").isEmpty());
    EXPECT_TRUE(cache.stagingPath("8").isEmpty());
    cache.invalidate("8");
    for (const auto& path : files)
        EXPECT_TRUE(QFile::exists(path)) << path.toStdString();
    EXPECT_EQ(QFile::permissions(outside.path()), permissions);
}

TEST(ArtworkCache, SubstitutedStagingParentsCannotDeleteOrChmodRegisteredExternalPaths)
{
    for (const bool replaceRoot : {false, true}) {
        SCOPED_TRACE(replaceRoot ? "root" : "staging");
        QTemporaryDir dir;
        QTemporaryDir outside;
        const auto root = dir.filePath("cache");
        ArtworkCache cache(root);
        reconcile(cache, "scope", {"7"}, {});
        const QStringList stages{cache.stagingPath("7"), cache.stagingPath("7"), cache.stagingPath("7")};
        const auto externalDir = replaceRoot ? outside.filePath("staging") : outside.path();
        ASSERT_TRUE(QDir().mkpath(externalDir));
        QStringList externalFiles;
        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(Qt::green);
        for (const auto& stage : stages) {
            ASSERT_FALSE(stage.isEmpty());
            const auto external = externalDir + u'/' + QFileInfo(stage).fileName();
            ASSERT_TRUE(image.save(external, "PNG"));
            ASSERT_TRUE(QFile::copy(external, external + QStringLiteral(".part")));
            externalFiles.append(external);
            externalFiles.append(external + QStringLiteral(".part"));
        }
        const auto substituted = replaceRoot ? root : root + QStringLiteral("/staging");
        ASSERT_TRUE(QDir(substituted).removeRecursively());
        ASSERT_TRUE(QFile::link(outside.path(), substituted));
        QList<QFileDevice::Permissions> permissions;
        for (const auto& path : externalFiles)
            permissions.append(QFile::permissions(path));
        const auto directoryPermissions = QFile::permissions(externalDir);
        cache.discard(stages[0]);
        EXPECT_TRUE(cache.store("7", stages[1], cache.generation()).isEmpty());
        EXPECT_TRUE(cache.store("7", stages[2], cache.generation() + 1).isEmpty());
        for (qsizetype i = 0; i < externalFiles.size(); ++i) {
            EXPECT_TRUE(QFile::exists(externalFiles[i])) << externalFiles[i].toStdString();
            EXPECT_EQ(QFile::permissions(externalFiles[i]), permissions[i]);
        }
        EXPECT_EQ(QFile::permissions(externalDir), directoryPermissions);
    }
}

TEST(ArtworkCache, DeferredLookupRejectsAccountChangesAndRevokedAccess)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path());
    reconcile(cache, "one", {"7", "8"}, {});
    ASSERT_FALSE(put(cache, "7").isEmpty());
    const auto result = ArtworkCache::validateLookup(cache.prepareLookup("7"));
    ASSERT_TRUE(result.valid);
    reconcile(cache, "one", {"8"}, {});
    EXPECT_TRUE(cache.finishLookup(result).isEmpty());
    reconcile(cache, "two", {"7"}, {});
    const auto replacement = put(cache, "7", Qt::blue);
    ASSERT_FALSE(replacement.isEmpty());
    EXPECT_TRUE(cache.finishLookup(result).isEmpty());
    EXPECT_EQ(cache.lookup("7"), replacement);
}

TEST(ArtworkCache, DeferredLookupDoesNotAcceptOrDeleteReplacementFiles)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path());
    reconcile(cache, "scope", {"7"}, {});
    const auto path = put(cache, "7");
    ASSERT_FALSE(path.isEmpty());
    const auto old = cache.prepareLookup("7");
    const auto valid = ArtworkCache::validateLookup(old);
    ASSERT_TRUE(valid.valid);
    ASSERT_TRUE(QFile::remove(path));
    const auto replacement = put(cache, "7", Qt::blue);
    ASSERT_EQ(path, replacement);
    EXPECT_TRUE(cache.finishLookup(valid).isEmpty());
    const auto obsolete = ArtworkCache::validateLookup(old);
    EXPECT_FALSE(obsolete.valid);
    EXPECT_TRUE(cache.finishLookup(obsolete).isEmpty());
    EXPECT_EQ(cache.lookup("7"), replacement);
    EXPECT_EQ(QImage(replacement).pixelColor(0, 0), QColor(Qt::blue));
}

TEST(ArtworkCache, DeferredLookupRejectsCorruptionWithoutWorkerSideEffects)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path());
    reconcile(cache, "scope", {"7"}, {});
    const auto path = put(cache, "7");
    ASSERT_FALSE(path.isEmpty());
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("corrupt");
    file.close();
    const auto invalid = ArtworkCache::validateLookup(cache.prepareLookup("7"));
    EXPECT_FALSE(invalid.valid);
    EXPECT_TRUE(QFile::exists(path));
    EXPECT_TRUE(cache.finishLookup(invalid).isEmpty());
    EXPECT_FALSE(QFile::exists(path));
    EXPECT_FALSE(put(cache, "7").isEmpty());
}

TEST(ArtworkCache, DeferredLookupRejectsDirectorySubstitutionBeforeAndAfterDecode)
{
    for (const bool afterDecode : {false, true}) {
        QTemporaryDir dir;
        QTemporaryDir outside;
        ArtworkCache cache(dir.path());
        reconcile(cache, "scope", {"7"}, {});
        const auto path = put(cache, "7");
        ASSERT_FALSE(path.isEmpty());
        auto result = cache.prepareLookup("7");
        if (afterDecode) {
            result = ArtworkCache::validateLookup(result);
            ASSERT_TRUE(result.valid);
        }
        const QFileInfo entry(path);
        const auto external = outside.filePath(entry.fileName());
        ASSERT_TRUE(QFile::copy(path, external));
        ASSERT_TRUE(QFile::remove(path));
        ASSERT_TRUE(QDir().rmdir(entry.absolutePath()));
        ASSERT_TRUE(QFile::link(outside.path(), entry.absolutePath()));
        if (!afterDecode) {
            result = ArtworkCache::validateLookup(result);
            EXPECT_FALSE(result.valid);
        }
        EXPECT_TRUE(cache.finishLookup(result).isEmpty());
        EXPECT_TRUE(QFile::exists(external));
    }
}

TEST(ArtworkCache, DeferredStoreDecodesWithoutPublishingOrConsumingStaging)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path());
    reconcile(cache, "scope", {"7"}, {});
    const QString stage = cache.stagingPath("7");
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(Qt::green);
    ASSERT_TRUE(image.save(stage, "PNG"));
    const auto result = ArtworkCache::validateStore(cache.prepareStore("7", stage, cache.generation()));
    ASSERT_TRUE(result.staging.valid);
    EXPECT_TRUE(QFile::exists(stage));
    EXPECT_FALSE(QFile::exists(result.destination.path));
    const QString path = cache.finishStore(result);
    ASSERT_FALSE(path.isEmpty());
    EXPECT_FALSE(QFile::exists(stage));
    EXPECT_EQ(QImage(path).pixelColor(0, 0), QColor(Qt::green));
    EXPECT_TRUE(cache.finishStore(result).isEmpty());
}

TEST(ArtworkCache, DeferredStoreRejectsRevocationAndAccountChangeAfterDecode)
{
    for (const bool accountChange : {false, true}) {
        QTemporaryDir dir;
        ArtworkCache cache(dir.path());
        reconcile(cache, "one", {"7", "8"}, {});
        const QString stage = cache.stagingPath("7");
        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(Qt::red);
        ASSERT_TRUE(image.save(stage, "PNG"));
        const auto result = ArtworkCache::validateStore(cache.prepareStore("7", stage, cache.generation()));
        ASSERT_TRUE(result.staging.valid);
        reconcile(cache, accountChange ? "two" : "one", {"8"}, {});
        EXPECT_TRUE(cache.finishStore(result).isEmpty());
        EXPECT_FALSE(QFile::exists(stage));
        EXPECT_FALSE(QFile::exists(result.destination.path));
        EXPECT_FALSE(cache.stagingFull());
    }
}

TEST(ArtworkCache, DeferredStoreCannotOverwriteDestinationCreatedDuringDecode)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path());
    reconcile(cache, "scope", {"7"}, {});
    const QString stage = cache.stagingPath("7");
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(Qt::red);
    ASSERT_TRUE(image.save(stage, "PNG"));
    const auto result = ArtworkCache::validateStore(cache.prepareStore("7", stage, cache.generation()));
    ASSERT_TRUE(result.staging.valid);
    const QString newer = put(cache, "7", Qt::blue);
    ASSERT_FALSE(newer.isEmpty());
    EXPECT_TRUE(cache.finishStore(result).isEmpty());
    EXPECT_EQ(QImage(newer).pixelColor(0, 0), QColor(Qt::blue));
}

TEST(ArtworkCache, DeferredStoreRejectsChangedStagingAfterDecode)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path());
    reconcile(cache, "scope", {"7"}, {});
    const QString stage = cache.stagingPath("7");
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(Qt::red);
    ASSERT_TRUE(image.save(stage, "PNG"));
    const auto result = ArtworkCache::validateStore(cache.prepareStore("7", stage, cache.generation()));
    ASSERT_TRUE(result.staging.valid);
    image.fill(Qt::blue);
    ASSERT_TRUE(image.save(stage, "PNG"));
    EXPECT_TRUE(cache.finishStore(result).isEmpty());
    EXPECT_FALSE(QFile::exists(result.destination.path));
}

TEST(ArtworkCache, CleanupDefersDiskMutationAndRejectsNewAuthorization)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path());
    reconcile(cache, "scope", {"7"}, {"7"});
    const auto path = put(cache, "7");
    ASSERT_FALSE(path.isEmpty());
    EXPECT_TRUE(cache.reconcile("scope", {}, {}).contains("7"));
    EXPECT_TRUE(cache.lookup("7").isEmpty());
    EXPECT_TRUE(QFile::exists(path));
    auto cleanup = ArtworkCache::scanCleanup(cache.prepareCleanup());
    EXPECT_TRUE(QFile::exists(path));
    cache.reconcile("scope", {"7"}, {"7"});
    EXPECT_EQ(cache.finishCleanup(cleanup), ArtworkCache::CleanupResult::Stale);
    EXPECT_TRUE(QFile::exists(path));
    cleanup = ArtworkCache::scanCleanup(cache.prepareCleanup());
    EXPECT_EQ(cache.finishCleanup(cleanup), ArtworkCache::CleanupResult::Done);
    EXPECT_EQ(cache.lookup("7"), path);
}

TEST(ArtworkCache, CleanupDeletesInBoundedBatchesAndRejectsAccountChanges)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path());
    reconcile(cache, "scope", {"7"}, {});
    const auto path = put(cache, "7");
    ASSERT_FALSE(path.isEmpty());
    QDir scope(QFileInfo(path).absolutePath());
    for (int i = 0; i < 80; ++i) {
        QFile file(scope.filePath(QString::number(i) + ".image"));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write("obsolete"), 8);
    }
    cache.reconcile("scope", {}, {});
    auto cleanup = ArtworkCache::scanCleanup(cache.prepareCleanup());
    EXPECT_EQ(scope.entryList(QDir::Files).size(), 81);
    EXPECT_EQ(cache.finishCleanup(cleanup), ArtworkCache::CleanupResult::More);
    EXPECT_EQ(scope.entryList(QDir::Files).size(), 49);
    cache.reconcile("other", {"7"}, {});
    EXPECT_EQ(cache.finishCleanup(cleanup), ArtworkCache::CleanupResult::Stale);
    EXPECT_EQ(scope.entryList(QDir::Files).size(), 49);
}

TEST(ArtworkCache, CleanupNeverDeletesReplacementFilesOrSubstitutedDirectories)
{
    for (int replacement = 0; replacement < 3; ++replacement) {
        SCOPED_TRACE(replacement);
        QTemporaryDir dir, outside;
        const QString root = dir.filePath("cache");
        ArtworkCache cache(root);
        reconcile(cache, "scope", {"7"}, {});
        const auto path = put(cache, "7");
        ASSERT_FALSE(path.isEmpty());
        cache.reconcile("scope", {}, {});
        auto cleanup = ArtworkCache::scanCleanup(cache.prepareCleanup());
        QString retained;
        if (replacement == 0) {
            ASSERT_TRUE(QFile::remove(path));
            retained = path;
        } else {
            const auto parent = replacement == 1 ? QFileInfo(path).absolutePath() : root;
            ASSERT_TRUE(QDir(parent).removeRecursively());
            const auto target = replacement == 1 ? outside.path() : outside.filePath(QFileInfo(path).dir().dirName());
            ASSERT_TRUE(QDir().mkpath(target));
            ASSERT_TRUE(QFile::link(outside.path(), parent));
            retained = target + '/' + QFileInfo(path).fileName();
        }
        QFile file(retained);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write("replacement"), 11);
        file.close();
        while (cache.finishCleanup(cleanup) == ArtworkCache::CleanupResult::More) { }
        EXPECT_TRUE(QFile::exists(retained));
    }
}

TEST(ArtworkCache, CleanupPreservesRegisteredStagingAndExpiresOnlyAbandonedFiles)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path());
    reconcile(cache, "scope", {"7"}, {});
    const QString registered = cache.stagingPath("7");
    const QString abandoned = QFileInfo(registered).dir().filePath("abandoned.image");
    for (const auto& path : {registered, registered + ".part", abandoned, abandoned + ".part"}) {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write("staging"), 7);
        ASSERT_TRUE(file.flush());
        ASSERT_TRUE(file.setFileTime(QDateTime::currentDateTimeUtc().addDays(-2), QFileDevice::FileModificationTime));
    }
    auto cleanup = ArtworkCache::scanCleanup(cache.prepareCleanup());
    EXPECT_TRUE(QFile::exists(abandoned));
    EXPECT_EQ(cache.finishCleanup(cleanup), ArtworkCache::CleanupResult::Done);
    EXPECT_TRUE(QFile::exists(registered));
    EXPECT_TRUE(QFile::exists(registered + ".part"));
    EXPECT_FALSE(QFile::exists(abandoned));
    EXPECT_FALSE(QFile::exists(abandoned + ".part"));
}

TEST(ArtworkCache, CleanupBudgetEvictsOldScopesAndPreservesCurrentPins)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path(), {1024 * 1024, 1, 1024 * 1024});
    reconcile(cache, "old", {"7"}, {});
    const auto old = put(cache, "7");
    ASSERT_FALSE(old.isEmpty());
    cache.reconcile("current", {"7"}, {"7"});
    const auto lookup = cache.prepareLookup("7");
    ASSERT_TRUE(QFile::copy(old, lookup.path));
    auto cleanup = ArtworkCache::scanCleanup(cache.prepareCleanup());
    EXPECT_EQ(cache.finishCleanup(cleanup), ArtworkCache::CleanupResult::Done);
    EXPECT_FALSE(QFile::exists(old));
    EXPECT_TRUE(QFile::exists(lookup.path));
}

namespace {
ArtworkCache::Store deferredStore(ArtworkCache& cache, const QString& id)
{
    const auto stage = cache.stagingPath(id);
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(Qt::green);
    if (stage.isEmpty() || !image.save(stage, "PNG"))
        return {};
    return ArtworkCache::validateStore(cache.prepareStore(id, stage, cache.generation()));
}
} // namespace

TEST(ArtworkCache, StorePruningReservesCapacityAndDeletesInBoundedBatches)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path(), {1024 * 1024, 1, 1024 * 1024});
    reconcile(cache, "old", {"old"}, {});
    const auto old = put(cache, "old");
    ASSERT_FALSE(old.isEmpty());
    const QDir scope(QFileInfo(old).absolutePath());
    for (int i = 0; i < 80; ++i) {
        QFile file(scope.filePath(QString::number(i) + ".image"));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write("old"), 3);
    }
    cache.reconcile("new", {"7"}, {"7"});
    auto store = deferredStore(cache, "7");
    ASSERT_TRUE(store.staging.valid);
    ASSERT_TRUE(store.cleanup.valid);
    // Scanning never mutates the cache or publishes the staging image.
    EXPECT_EQ(scope.entryList(QDir::Files).size(), 81);
    EXPECT_FALSE(QFile::exists(store.destination.path));
    QString path;
    EXPECT_EQ(cache.finishStore(store, path), ArtworkCache::CleanupResult::More);
    EXPECT_TRUE(path.isEmpty());
    EXPECT_TRUE(QFile::exists(store.staging.path));
    EXPECT_EQ(scope.entryList(QDir::Files).size(), 49);
    EXPECT_EQ(cache.finishStore(store, path), ArtworkCache::CleanupResult::More);
    EXPECT_EQ(scope.entryList(QDir::Files).size(), 17);
    EXPECT_EQ(cache.finishStore(store, path), ArtworkCache::CleanupResult::Done);
    EXPECT_EQ(path, store.destination.path);
    EXPECT_TRUE(scope.entryList(QDir::Files).isEmpty());
    EXPECT_FALSE(QFile::exists(store.staging.path));
}

TEST(ArtworkCache, CompetingStoresRefreshReservationBeforePublication)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path(), {1024 * 1024, 1, 1024 * 1024});
    reconcile(cache, "scope", {"7", "8"}, {"7"});
    auto first = deferredStore(cache, "7");
    auto second = deferredStore(cache, "8");
    QString path;
    // Registering the second staging owner invalidates the first scan.
    EXPECT_EQ(cache.finishStore(first, path), ArtworkCache::CleanupResult::Stale);
    first = ArtworkCache::scanStoreCleanup(cache.prepareStoreCleanup(first));
    EXPECT_EQ(cache.finishStore(first, path), ArtworkCache::CleanupResult::Done);
    ASSERT_FALSE(path.isEmpty());
    const auto pinned = path;
    EXPECT_EQ(cache.finishStore(second, path), ArtworkCache::CleanupResult::Stale);
    second = ArtworkCache::scanStoreCleanup(cache.prepareStoreCleanup(second));
    EXPECT_EQ(cache.finishStore(second, path), ArtworkCache::CleanupResult::Done);
    EXPECT_TRUE(path.isEmpty());
    EXPECT_TRUE(QFile::exists(pinned));
    EXPECT_FALSE(QFile::exists(second.staging.path));
    EXPECT_FALSE(QFile::exists(second.destination.path));
}

TEST(ArtworkCache, StorePruningRefreshesPinsAndLruBeforeEvicting)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path(), {1024 * 1024, 1, 1024 * 1024});
    reconcile(cache, "scope", {"7", "8"}, {});
    const auto old = put(cache, "7");
    ASSERT_FALSE(old.isEmpty());
    auto store = deferredStore(cache, "8");
    EXPECT_EQ(cache.lookup("7"), old);
    QString path;
    EXPECT_EQ(cache.finishStore(store, path), ArtworkCache::CleanupResult::Stale);
    EXPECT_TRUE(QFile::exists(old));
    cache.reconcile("scope", {"7", "8"}, {"7"});
    store = ArtworkCache::scanStoreCleanup(cache.prepareStoreCleanup(store));
    EXPECT_EQ(cache.finishStore(store, path), ArtworkCache::CleanupResult::Done);
    EXPECT_TRUE(path.isEmpty());
    EXPECT_TRUE(QFile::exists(old));
}

TEST(ArtworkCache, StorePruningRejectsRevocationBetweenBatches)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path(), {1024 * 1024, 1, 1024 * 1024});
    reconcile(cache, "old", {"old"}, {});
    const auto old = put(cache, "old");
    ASSERT_FALSE(old.isEmpty());
    const QDir scope(QFileInfo(old).absolutePath());
    for (int i = 0; i < 40; ++i) {
        QFile file(scope.filePath(QString::number(i) + ".image"));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write("old"), 3);
    }
    cache.reconcile("new", {"7", "8"}, {});
    auto store = deferredStore(cache, "7");
    QString path;
    EXPECT_EQ(cache.finishStore(store, path), ArtworkCache::CleanupResult::More);
    EXPECT_EQ(scope.entryList(QDir::Files).size(), 9);
    cache.reconcile("new", {"8"}, {});
    EXPECT_EQ(cache.finishStore(store, path), ArtworkCache::CleanupResult::Done);
    EXPECT_TRUE(path.isEmpty());
    EXPECT_EQ(scope.entryList(QDir::Files).size(), 9);
    EXPECT_FALSE(QFile::exists(store.staging.path));
    EXPECT_FALSE(QFile::exists(store.destination.path));
}

TEST(ArtworkCache, StorePruningReplacementCandidateCannotFundAdmission)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path(), {1024 * 1024, 1, 1024 * 1024});
    reconcile(cache, "scope", {"7", "8"}, {});
    const auto old = put(cache, "7");
    ASSERT_FALSE(old.isEmpty());
    auto store = deferredStore(cache, "8");
    ASSERT_TRUE(QFile::remove(old));
    QFile replacement(old);
    ASSERT_TRUE(replacement.open(QIODevice::WriteOnly));
    ASSERT_EQ(replacement.write("replacement"), 11);
    replacement.close();
    QString path;
    EXPECT_EQ(cache.finishStore(store, path), ArtworkCache::CleanupResult::Done);
    EXPECT_TRUE(path.isEmpty());
    EXPECT_TRUE(QFile::exists(old));
    EXPECT_FALSE(QFile::exists(store.destination.path));
}

TEST(ArtworkCache, StoreReservationCountsBytesAndReplacesCorruptPinnedDestination)
{
    QTemporaryDir dir;
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(Qt::green);
    const auto sample = dir.filePath("sample.png");
    ASSERT_TRUE(image.save(sample));
    const qint64 bytes = QFileInfo(sample).size();
    ArtworkCache cache(dir.filePath("cache"), {bytes, 10, 1024 * 1024});
    reconcile(cache, "scope", {"7", "8"}, {"7"});
    const auto first = put(cache, "7", Qt::green);
    ASSERT_FALSE(first.isEmpty());
    auto other = deferredStore(cache, "8");
    QString path;
    EXPECT_EQ(cache.finishStore(other, path), ArtworkCache::CleanupResult::Done);
    EXPECT_TRUE(path.isEmpty());
    EXPECT_TRUE(QFile::exists(first));
    QFile corrupt(first);
    ASSERT_TRUE(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(corrupt.write("bad"), 3);
    corrupt.close();
    auto replacement = deferredStore(cache, "7");
    ASSERT_FALSE(replacement.destination.valid);
    EXPECT_EQ(cache.finishStore(replacement, path), ArtworkCache::CleanupResult::Done);
    EXPECT_EQ(path, first);
    EXPECT_EQ(QFileInfo(path).size(), bytes);
    EXPECT_FALSE(QImage(path).isNull());
}

TEST(ArtworkCache, StorePruningRejectsIncompleteDirectoryEnumeration)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path(), {1024 * 1024, 1, 1024 * 1024});
    reconcile(cache, "old", {"7"}, {});
    const auto old = put(cache, "7");
    ASSERT_FALSE(old.isEmpty());
    cache.reconcile("new", {"8"}, {});
    const auto parent = QFileInfo(old).absolutePath();
    ASSERT_TRUE(QFile::setPermissions(parent, {}));
    auto store = deferredStore(cache, "8");
    EXPECT_FALSE(store.cleanup.valid);
    QString path;
    EXPECT_EQ(cache.finishStore(store, path), ArtworkCache::CleanupResult::Done);
    EXPECT_TRUE(path.isEmpty());
    EXPECT_FALSE(QFile::exists(store.destination.path));
    EXPECT_TRUE(
        QFile::setPermissions(parent, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    EXPECT_TRUE(QFile::exists(old));
}
