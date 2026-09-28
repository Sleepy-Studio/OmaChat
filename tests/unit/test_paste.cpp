#include "text/PasteContent.hpp"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QMimeData>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using omachat::client::PasteContent;
using Kind = PasteContent::Kind;

namespace {

QByteArray encoded(const char* format)
{
    QImage image(4, 3, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, format);
    return bytes;
}

QByteArray readAll(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

TEST(Paste, PlainTextIsText)
{
    QMimeData mime;
    mime.setText(QStringLiteral("hello"));
    EXPECT_EQ(PasteContent::classify(mime), Kind::Text);
}

TEST(Paste, ScreenshotIsImage)
{
    QMimeData mime;
    mime.setData(QStringLiteral("image/png"), encoded("PNG"));
    EXPECT_EQ(PasteContent::classify(mime), Kind::Image);
}

TEST(Paste, ImageNextToTextIsText)
{
    // What a spreadsheet puts on the clipboard when cells are copied.
    QMimeData mime;
    mime.setData(QStringLiteral("image/png"), encoded("PNG"));
    mime.setText(QStringLiteral("a\tb\n1\t2"));
    EXPECT_EQ(PasteContent::classify(mime), Kind::Text);
}

TEST(Paste, LocalFilesAreFilesButWebLinksAreText)
{
    QMimeData files;
    files.setUrls({QUrl::fromLocalFile(QStringLiteral("/tmp/a.txt")), QUrl::fromLocalFile(QStringLiteral("/tmp/b"))});
    EXPECT_EQ(PasteContent::classify(files), Kind::Files);
    EXPECT_EQ(PasteContent::localFiles(files).size(), 2);

    QMimeData mixed;
    mixed.setUrls({QUrl::fromLocalFile(QStringLiteral("/tmp/a.txt")), QUrl(QStringLiteral("https://example.org/"))});
    mixed.setText(QStringLiteral("https://example.org/"));
    EXPECT_EQ(PasteContent::classify(mixed), Kind::Text);
    EXPECT_TRUE(PasteContent::localFiles(mixed).isEmpty());
}

TEST(Paste, EncodedImagesAreSavedByteForByte)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QByteArray jpeg = encoded("JPEG");
    QMimeData mime;
    mime.setData(QStringLiteral("image/jpeg"), jpeg);
    const QString path = PasteContent::saveImage(mime, dir.path());
    ASSERT_FALSE(path.isEmpty());
    EXPECT_TRUE(path.endsWith(QStringLiteral(".jpg")));
    EXPECT_TRUE(path.section(QLatin1Char('/'), -1).startsWith(QStringLiteral("pasted-")));
    EXPECT_EQ(readAll(path), jpeg);

    // A second paste in the same second gets its own file.
    const QString second = PasteContent::saveImage(mime, dir.path());
    ASSERT_FALSE(second.isEmpty());
    EXPECT_NE(second, path);
}

TEST(Paste, OtherImagesAreReencodedAsPng)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QImage image(5, 5, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    QMimeData mime;
    mime.setImageData(image);
    const QString path = PasteContent::saveImage(mime, dir.path());
    ASSERT_FALSE(path.isEmpty());
    EXPECT_TRUE(path.endsWith(QStringLiteral(".png")));
    EXPECT_EQ(QImage(path).size(), QSize(5, 5));
}

TEST(Paste, CorruptImageIsRejected)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QMimeData mime;
    mime.setData(QStringLiteral("image/png"), QByteArray("not a png"));
    EXPECT_TRUE(PasteContent::saveImage(mime, dir.path()).isEmpty());
}
