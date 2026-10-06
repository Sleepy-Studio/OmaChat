#include "platform/ArtworkCrop.hpp"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <limits>

using namespace omachat::client;

TEST(ArtworkCrop, FocalControlsReachEdgesAndZoomRemainsInsideSource)
{
    EXPECT_EQ(artwork::cropRect({1200, 600}, "icon", 0, 0, 1), QRect(0, 0, 600, 600));
    EXPECT_EQ(artwork::cropRect({1200, 600}, "icon", 1, 1, 1), QRect(600, 0, 600, 600));
    EXPECT_EQ(artwork::cropRect({1200, 600}, "banner", 1, 1, 2), QRect(600, 450, 600, 150));
    EXPECT_TRUE(artwork::cropRect({1200, 600}, "banner", 0.5, 0.5, 0).isEmpty());
    EXPECT_TRUE(artwork::cropRect({1200, 600}, "unknown", 0.5, 0.5, 1).isEmpty());
    EXPECT_TRUE(artwork::cropRect({1200, 600}, "icon", std::numeric_limits<double>::quiet_NaN(), 0.5, 1).isEmpty());
}

TEST(ArtworkCrop, CanonicalRasterIsBoundedAndPreservesChosenRegion)
{
    QImage source(100, 50, QImage::Format_RGB32);
    source.fill(Qt::red);
    for (int y = 0; y < 50; ++y)
        for (int x = 50; x < 100; ++x)
            source.setPixelColor(x, y, Qt::blue);
    QString error;
    auto bytes = artwork::encodeCrop(source, "icon", 1, 0.5, 1, &error);
    ASSERT_TRUE(error.isEmpty());
    ASSERT_LE(bytes.size(), artwork::maxSourceBytes);
    const QImage icon = QImage::fromData(bytes, "PNG");
    EXPECT_EQ(icon.size(), QSize(512, 512));
    EXPECT_EQ(icon.pixelColor(256, 256), QColor(Qt::blue));
    bytes = artwork::encodeCrop(source, "banner", 0.5, 0.5, 1, &error);
    EXPECT_EQ(QImage::fromData(bytes).size(), QSize(1024, 256));
}

TEST(ArtworkCrop, SourceValidationRejectsCorruptLargeAndOversizedDimensions)
{
    QTemporaryDir dir;
    const QString path = dir.filePath("source.png");
    QString error;
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("not an image");
    file.close();
    EXPECT_TRUE(artwork::readSource(path, &error).isNull());
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QByteArray(artwork::maxSourceBytes + 1, 'x'));
    file.close();
    EXPECT_TRUE(artwork::readSource(path, &error).isNull());
    QImage oversized(8193, 1, QImage::Format_RGB32);
    oversized.fill(Qt::red);
    ASSERT_TRUE(oversized.save(path));
    EXPECT_TRUE(artwork::readSource(path, &error).isNull());
    QImage normal(50, 100, QImage::Format_RGB32);
    normal.fill(Qt::green);
    ASSERT_TRUE(normal.save(path));
    error.clear();
    EXPECT_EQ(artwork::readSource(path, &error).size(), normal.size());
    EXPECT_TRUE(error.isEmpty());
}

TEST(ArtworkCrop, AnimatedAndUnsupportedRasterCannotEnterStaticCrop)
{
    QTemporaryDir dir;
    QFile file(dir.filePath("animation.gif"));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    // A valid one-pixel GIF remains unsupported even when its extension is disguised.
    file.write(QByteArray::fromBase64("R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7"));
    file.close();
    QString error;
    EXPECT_TRUE(artwork::readSource(file.fileName(), &error).isNull());
    EXPECT_FALSE(error.isEmpty());
}

TEST(ArtworkCrop, SupportedStaticFormatsDecodeWithinTheSameLimits)
{
    QTemporaryDir dir;
    QImage source(120, 80, QImage::Format_RGB32);
    source.fill(Qt::green);
    for (const char* format : {"PNG", "JPEG", "WEBP"}) {
        const QString path = dir.filePath(QString::fromLatin1(format) + ".image");
        ASSERT_TRUE(source.save(path, format)) << format;
        QString error;
        EXPECT_EQ(artwork::readSource(path, &error).size(), source.size()) << format;
        EXPECT_TRUE(error.isEmpty()) << error.toStdString();
    }
}

TEST(ArtworkCrop, FingerprintBindsPixelsAndDimensionsWithoutSourceMetadata)
{
    QImage source(120, 80, QImage::Format_RGB32);
    source.fill(Qt::green);
    const QString original = artwork::fingerprint(source);
    source.setText("description", "untrusted source metadata");
    EXPECT_EQ(artwork::fingerprint(source), original);
    source.setPixelColor(0, 0, Qt::red);
    EXPECT_NE(artwork::fingerprint(source), original);
    EXPECT_NE(artwork::fingerprint(source.scaled(80, 120)), original);
}
