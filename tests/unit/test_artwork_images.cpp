#include "core/ArtworkImages.hpp"

#include <QBuffer>
#include <QImage>
#include <gtest/gtest.h>

using namespace omachat;
namespace {
QByteArray png(const QImage& image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    EXPECT_TRUE(image.save(&buffer, "PNG"));
    return bytes;
}
proto::ArtworkFraming framing()
{
    proto::ArtworkFraming f;
    f.set_kind(proto::ArtworkFraming::ICON);
    f.set_source_width(100);
    f.set_source_height(50);
    f.set_x(50);
    f.set_width(50);
    f.set_height(50);
    return f;
}
} // namespace
TEST(ArtworkImages, GeneratesSharedFramingVariantsAndRemovesMetadata)
{
    QImage image(100, 50, QImage::Format_RGB32);
    image.fill(Qt::red);
    for (int y = 0; y < 50; ++y)
        for (int x = 50; x < 100; ++x)
            image.setPixelColor(x, y, Qt::blue);
    image.setText("Description", "private source identity");
    const auto f = framing();
    const auto images = server::artwork::prepare(png(image), &f);
    ASSERT_TRUE(images.error.isEmpty()) << images.error.toStdString();
    EXPECT_EQ(QImage::fromData(images.full).size(), QSize(512, 512));
    EXPECT_EQ(QImage::fromData(images.small).size(), QSize(128, 128));
    EXPECT_EQ(QImage::fromData(images.source).size(), image.size());
    EXPECT_EQ(QImage::fromData(images.full).pixelColor(256, 256), QColor(Qt::blue));
    EXPECT_EQ(QImage::fromData(images.small).pixelColor(64, 64), QColor(Qt::blue));
    for (const auto& bytes : {images.full, images.small, images.source}) {
        EXPECT_TRUE(QImage::fromData(bytes).textKeys().isEmpty());
        EXPECT_FALSE(bytes.contains("private source identity"));
        EXPECT_LE(bytes.size(), 2 * 1024 * 1024);
    }
}
TEST(ArtworkImages, LegacyDownloadsRemainFullAndStripMetadataWithoutResizing)
{
    QImage image(32, 16, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    image.setText("Author", "hidden author");
    const auto images = server::artwork::prepare(png(image), nullptr);
    ASSERT_TRUE(images.error.isEmpty());
    EXPECT_EQ(QImage::fromData(images.full).size(), image.size());
    EXPECT_TRUE(QImage::fromData(images.full).textKeys().isEmpty());
    EXPECT_TRUE(images.small.isEmpty());
    EXPECT_TRUE(images.source.isEmpty());
}
TEST(ArtworkImages, RejectsInvalidFramingDimensionsBoundsAndUnboundedInput)
{
    auto f = framing();
    ASSERT_TRUE(server::artwork::validFraming(f));
    f.set_x(51);
    EXPECT_FALSE(server::artwork::validFraming(f));
    f = framing();
    f.set_height(49);
    EXPECT_FALSE(server::artwork::validFraming(f));
    f = framing();
    f.set_width(11);
    f.set_height(11);
    EXPECT_FALSE(server::artwork::validFraming(f));
    f = framing();
    f.set_source_width(0xffffffffu);
    EXPECT_FALSE(server::artwork::validFraming(f));
    EXPECT_FALSE(server::artwork::prepare("bad", nullptr).error.isEmpty());
    EXPECT_FALSE(server::artwork::prepare(QByteArray(2 * 1024 * 1024 + 1, 'a'), nullptr).error.isEmpty());
    QImage image(99, 50, QImage::Format_RGB32);
    image.fill(Qt::blue);
    f = framing();
    EXPECT_FALSE(server::artwork::prepare(png(image), &f).error.isEmpty());
}
TEST(ArtworkImages, BannerUsesIndependentBoundedVariantAndSupportsThinSources)
{
    auto f = framing();
    f.set_kind(proto::ArtworkFraming::BANNER);
    f.set_x(0);
    f.set_width(100);
    f.set_height(25);
    QImage image(100, 50, QImage::Format_RGB32);
    image.fill(Qt::green);
    const auto images = server::artwork::prepare(png(image), &f);
    ASSERT_TRUE(images.error.isEmpty());
    EXPECT_EQ(QImage::fromData(images.full).size(), QSize(1024, 256));
    EXPECT_EQ(QImage::fromData(images.small).size(), QSize(512, 128));
    f.set_source_width(1);
    f.set_source_height(100);
    f.set_width(1);
    f.set_height(1);
    EXPECT_TRUE(server::artwork::validFraming(f));
}
