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
    first.reconcile("account/host/443/user/1/pin-a", {"7"}, {"7"});
    const auto path = put(first, "7");
    ASSERT_FALSE(path.isEmpty());
    ArtworkCache restarted(dir.path());
    EXPECT_TRUE(restarted.lookup("7").isEmpty());
    restarted.reconcile("account/host/443/user/1/pin-a", {"7"}, {"7"});
    EXPECT_EQ(restarted.lookup("7"), path);
    EXPECT_FALSE(QFile::permissions(path) & (QFileDevice::ReadGroup | QFileDevice::ReadOther));
}

TEST(ArtworkCache, AccountEndpointAndAuthenticationIdentityNeverShareFiles)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path());
    cache.reconcile("account/host/443/user/1/pin-a", {"7"}, {});
    const auto original = put(cache, "7");
    ASSERT_FALSE(original.isEmpty());
    for (const auto* identity : {"other/host/443/user/1", "account/elsewhere/443/user/1", "account/host/8443/user/1",
             "account/host/443/user/2", "account/host/443/user/1/pin-b"}) {
        const auto generation = cache.generation();
        EXPECT_TRUE(cache.reconcile(identity, {"7"}, {}).contains("7"));
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
    cache.reconcile("scope", {"old"}, {"old"});
    const auto old = put(cache, "old");
    ASSERT_FALSE(old.isEmpty());
    EXPECT_TRUE(cache.reconcile("scope", {"new"}, {"new"}).contains("old"));
    EXPECT_FALSE(QFile::exists(old));
    const auto replacement = put(cache, "new");
    ASSERT_FALSE(replacement.isEmpty());
    EXPECT_TRUE(cache.reconcile("scope", {}, {}).contains("new"));
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
        cache.reconcile("scope", {"revoked"}, {});
        path = put(cache, "revoked");
        ASSERT_FALSE(path.isEmpty());
    }
    ArtworkCache cache(dir.path());
    cache.reconcile("scope", {}, {});
    EXPECT_FALSE(QFile::exists(path));
}

TEST(ArtworkCache, CorruptEntriesAndDownloadsAreRejectedAndCanRecover)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path());
    cache.reconcile("scope", {"7"}, {"7"});
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
    cache.reconcile("old-account", {"old"}, {});
    const auto old = put(cache, "old");
    cache.reconcile("current", {"active", "second", "third"}, {"active"});
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
    cache.reconcile("scope", {"active", "other"}, {"active"});
    const auto active = put(cache, "active");
    ASSERT_FALSE(active.isEmpty());
    EXPECT_TRUE(put(cache, "other").isEmpty());
    EXPECT_TRUE(QFile::exists(active));
}

TEST(ArtworkCache, ByteAndPerImageBudgetsRejectOversizeFiles)
{
    QTemporaryDir dir;
    ArtworkCache tinyBudget(dir.path(), {1, 10, 1024});
    tinyBudget.reconcile("scope", {"7"}, {});
    EXPECT_TRUE(put(tinyBudget, "7").isEmpty());
    ArtworkCache tinyImage(dir.path(), {1024, 10, 1});
    tinyImage.reconcile("scope", {"7"}, {});
    EXPECT_TRUE(put(tinyImage, "7").isEmpty());
}

TEST(ArtworkCache, InFlightCompletionCannotCrossAccountsOrRestoreRevokedAccess)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path());
    cache.reconcile("one", {"7"}, {});
    const auto stage = cache.stagingPath("7");
    const auto generation = cache.generation();
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(Qt::green);
    ASSERT_TRUE(image.save(stage, "PNG"));
    cache.reconcile("two", {"7"}, {});
    EXPECT_TRUE(cache.store("7", stage, generation).isEmpty());
    EXPECT_FALSE(QFile::exists(stage));
    const auto revoked = cache.stagingPath("7");
    ASSERT_TRUE(image.save(revoked, "PNG"));
    cache.reconcile("two", {}, {});
    EXPECT_TRUE(cache.store("7", revoked, cache.generation()).isEmpty());
    EXPECT_FALSE(QFile::exists(revoked));
}

TEST(ArtworkCache, ImmutableActiveEntryIsNeverReplacedByDuplicateDownload)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path());
    cache.reconcile("scope", {"7"}, {"7"});
    const auto first = put(cache, "7", Qt::red);
    ASSERT_FALSE(first.isEmpty());
    EXPECT_EQ(put(cache, "7", Qt::blue), first);
    EXPECT_EQ(QImage(first).pixelColor(0, 0), QColor(Qt::red));
}

TEST(ArtworkCache, UnknownStartupAuthorizationPreservesDiskUntilSnapshotArrives)
{
    QTemporaryDir dir;
    ArtworkCache first(dir.path());
    first.reconcile("scope", {"7"}, {});
    const auto path = put(first, "7");
    ASSERT_FALSE(path.isEmpty());
    ArtworkCache restarted(dir.path());
    restarted.reconcile("scope", {}, {}, false);
    EXPECT_TRUE(QFile::exists(path));
    EXPECT_TRUE(restarted.lookup("7").isEmpty());
    restarted.reconcile("scope", {"7"}, {"7"});
    EXPECT_EQ(restarted.lookup("7"), path);
    ArtworkCache revoked(dir.path());
    revoked.reconcile("scope", {}, {}, false);
    EXPECT_TRUE(QFile::exists(path));
    revoked.reconcile("scope", {}, {}, true);
    EXPECT_FALSE(QFile::exists(path));
}

TEST(ArtworkCache, ConcurrentStagingIsBoundedAndFailuresReleaseTheirSlots)
{
    QTemporaryDir dir;
    ArtworkCache cache(dir.path());
    cache.reconcile("scope", {"7"}, {});
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
    cache.reconcile("scope", {"7"}, {});
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
    cache.reconcile("scope", {"7"}, {});
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
    cache.reconcile("scope", {"8"}, {});
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
        cache.reconcile("scope", {"7"}, {});
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
