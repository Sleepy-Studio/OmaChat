#include "views/AudioLevels.hpp"

#include <QAudioFormat>
#include <QCoreApplication>
#include <QDataStream>
#include <QElapsedTimer>
#include <QFile>
#include <QMediaPlayer>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QUrl>

#include <gtest/gtest.h>

using omachat::client::AudioLevels;

TEST(AudioLevels, NormalizesDecodedPcm)
{
    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::UInt8);

    EXPECT_DOUBLE_EQ(AudioLevels::amplitude(QAudioBuffer()), 0);
    EXPECT_NEAR(AudioLevels::amplitude(QAudioBuffer(QByteArray(256, char(128)), format)), 0, 0.01);
    EXPECT_GT(AudioLevels::amplitude(QAudioBuffer(QByteArray(256, char(255)), format)), 0.9);
}

TEST(AudioLevels, RecentBarsFollowAudioAndReset)
{
    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::UInt8);

    AudioLevels levels;
    levels.output()->audioBufferReceived(QAudioBuffer(QByteArray(256, char(255)), format));
    QTest::qWait(55);
    EXPECT_GT(levels.bars().last().toDouble(), 0.9);

    levels.reset();
    for (const auto& bar : levels.bars())
        EXPECT_DOUBLE_EQ(bar.toDouble(), 0);
}

TEST(AudioLevels, PlayerDeliversDecodedFileAudio)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QFile file(dir.filePath(QStringLiteral("tone.wav")));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    constexpr int frames = 48000;
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + frames * 2);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(1) << quint32(48000) << quint32(96000) << quint16(2) << quint16(16);
    stream.writeRawData("data", 4);
    stream << quint32(frames * 2);
    for (int i = 0; i < frames; ++i)
        stream << qint16((i / 120) % 2 == 0 ? 16000 : -16000);
    file.close();

    AudioLevels levels;
    QMediaPlayer player;
    player.setAudioBufferOutput(levels.output());
    player.setSource(QUrl::fromLocalFile(file.fileName()));
    player.play();

    QElapsedTimer elapsed;
    elapsed.start();
    bool animated = false;
    while (elapsed.elapsed() < 5000 && !animated) {
        QCoreApplication::processEvents();
        for (const auto& bar : levels.bars())
            animated |= bar.toDouble() > 0.05;
        QThread::msleep(10);
    }
    EXPECT_TRUE(animated);
}
