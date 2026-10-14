#include "platform/ArtworkCropMemory.hpp"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <future>
#include <gtest/gtest.h>
#include <limits>

using namespace omachat::client::artwork;
namespace {
QString key(QString account = "account", QString server = "server", QString kind = "icon", QChar pixel = u'a')
{
    return cropMemoryKey(account, server, QString(64, pixel), kind);
}
QJsonObject read(const QString& root)
{
    QFile file(root + "/choices.json");
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
void write(const QString& root, const QJsonObject& object)
{
    QFile file(root + "/choices.json");
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_TRUE(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    ASSERT_GT(file.write(QJsonDocument(object).toJson()), 0);
}
} // namespace

TEST(ArtworkCropMemory, RestartRestoresOnlyMatchingAccountServerPixelsAndSlot)
{
    QTemporaryDir dir;
    const QString root = dir.filePath("crops");
    ASSERT_TRUE(saveCropChoice(root, key(), {1, 0.25, 2}));
    // The module has no process-local state: every read reloads the persisted file.
    const auto restored = loadCropChoice(root, key());
    ASSERT_TRUE(restored);
    EXPECT_EQ(restored->x, 1);
    EXPECT_EQ(restored->y, 0.25);
    EXPECT_EQ(restored->zoom, 2);
    EXPECT_FALSE(loadCropChoice(root, key("other account")));
    EXPECT_FALSE(loadCropChoice(root, key("account", "other server")));
    EXPECT_FALSE(loadCropChoice(root, key("account", "server", "banner")));
    EXPECT_FALSE(loadCropChoice(root, key("account", "server", "icon", u'b')));
    const QByteArray bytes = QJsonDocument(read(root)).toJson();
    EXPECT_FALSE(bytes.contains("account"));
    EXPECT_FALSE(bytes.contains("server"));
    EXPECT_FALSE(bytes.contains("source"));
    EXPECT_EQ(QFileInfo(root + "/choices.json").permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther), 0);
}

TEST(ArtworkCropMemory, InvalidChoicesAndIdentityDoNotReplaceAcceptedFraming)
{
    QTemporaryDir dir;
    ASSERT_TRUE(saveCropChoice(dir.path(), key(), {0, 1, 4}));
    for (const auto choice : {CropChoice{-0.1, 0, 1}, CropChoice{0, 1.1, 1}, CropChoice{0, 0, 0.99},
             CropChoice{0, 0, 4.01}, CropChoice{std::numeric_limits<double>::quiet_NaN(), 0, 1}})
        EXPECT_FALSE(saveCropChoice(dir.path(), key(), choice));
    EXPECT_TRUE(cropMemoryKey({}, "server", QString(64, u'a'), "icon").isEmpty());
    EXPECT_TRUE(cropMemoryKey("account", "server", "not a pixel digest", "icon").isEmpty());
    EXPECT_TRUE(cropMemoryKey("account", "server", QString(64, u'a'), "unknown").isEmpty());
    ASSERT_TRUE(loadCropChoice(dir.path(), key()));
    EXPECT_EQ(loadCropChoice(dir.path(), key())->zoom, 4);
}

TEST(ArtworkCropMemory, ExpiredFutureAndMalformedRecordsAreIgnoredAndPruned)
{
    QTemporaryDir dir;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QString old = key("old"), future = key("future"), malformed = key("malformed");
    write(dir.path(),
        {{"version", 1},
            {"choices",
                QJsonObject{
                    {old, QJsonObject{{"x", 0}, {"y", 0}, {"zoom", 1}, {"saved_at", double(now - 91LL * 86400000)}}},
                    {future, QJsonObject{{"x", 0}, {"y", 0}, {"zoom", 1}, {"saved_at", double(now + 86400000)}}},
                    {malformed, QJsonObject{{"x", "0"}, {"y", 0}, {"zoom", 1}, {"saved_at", double(now)}}}}}});
    EXPECT_FALSE(loadCropChoice(dir.path(), old));
    EXPECT_FALSE(loadCropChoice(dir.path(), future));
    EXPECT_FALSE(loadCropChoice(dir.path(), malformed));
    ASSERT_TRUE(saveCropChoice(dir.path(), key(), {}));
    EXPECT_EQ(read(dir.path()).value("choices").toObject().size(), 1);
}

TEST(ArtworkCropMemory, RetentionIsBoundedAndNewestExplicitChoiceSurvives)
{
    QTemporaryDir dir;
    for (int i = 0; i < 140; ++i)
        ASSERT_TRUE(saveCropChoice(dir.path(), key(QString::number(i)), {0, 0, 1}));
    EXPECT_EQ(read(dir.path()).value("choices").toObject().size(), 128);
    ASSERT_TRUE(saveCropChoice(dir.path(), key("newest"), {1, 1, 4}));
    ASSERT_TRUE(loadCropChoice(dir.path(), key("newest")));
    EXPECT_EQ(loadCropChoice(dir.path(), key("newest"))->zoom, 4);
    EXPECT_LE(QFileInfo(dir.filePath("choices.json")).size(), 64 * 1024);
}

TEST(ArtworkCropMemory, ConcurrentWritersMergeDifferentChoices)
{
    QTemporaryDir dir;
    auto first = std::async(std::launch::async, [&] { return saveCropChoice(dir.path(), key("one"), {0, 0, 1}); });
    auto second = std::async(std::launch::async, [&] { return saveCropChoice(dir.path(), key("two"), {1, 1, 4}); });
    ASSERT_TRUE(first.get());
    ASSERT_TRUE(second.get());
    EXPECT_TRUE(loadCropChoice(dir.path(), key("one")));
    EXPECT_TRUE(loadCropChoice(dir.path(), key("two")));
}

TEST(ArtworkCropMemory, UnsafePathsAndCorruptStoresCannotBeOverwritten)
{
    QTemporaryDir dir, external;
    ASSERT_TRUE(QFile::link(external.path(), dir.filePath("linked")));
    EXPECT_FALSE(saveCropChoice(dir.filePath("linked"), key(), {}));
    QFile sentinel(external.filePath("sentinel"));
    ASSERT_TRUE(sentinel.open(QIODevice::WriteOnly));
    sentinel.write("keep me");
    sentinel.close();
    ASSERT_TRUE(QFile::link(sentinel.fileName(), dir.filePath("choices.json")));
    EXPECT_FALSE(loadCropChoice(dir.path(), key()));
    EXPECT_FALSE(saveCropChoice(dir.path(), key(), {}));
    ASSERT_TRUE(sentinel.open(QIODevice::ReadOnly));
    EXPECT_EQ(sentinel.readAll(), "keep me");
    sentinel.close();
    ASSERT_TRUE(QFile::remove(dir.filePath("choices.json")));
    write(dir.path(), {{"version", 99}, {"choices", QJsonObject{}}});
    EXPECT_FALSE(saveCropChoice(dir.path(), key(), {}));
    EXPECT_EQ(read(dir.path()).value("version").toInt(), 99);
    QFile oversized(dir.filePath("choices.json"));
    ASSERT_TRUE(oversized.open(QIODevice::WriteOnly | QIODevice::Truncate));
    oversized.write(QByteArray(64 * 1024 + 1, 'x'));
    oversized.close();
    EXPECT_FALSE(loadCropChoice(dir.path(), key()));
    EXPECT_FALSE(saveCropChoice(dir.path(), key(), {}));
}
