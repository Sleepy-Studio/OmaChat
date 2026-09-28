#include "omachat/config/ClientConfig.hpp"

#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace omachat::config;

TEST(ClientConfig, MissingFileYieldsDefaults)
{
    QString error;
    const auto cfg = ClientConfig::load(QStringLiteral("/nonexistent/omachat.toml"), &error);
    EXPECT_TRUE(error.isEmpty());
    EXPECT_EQ(cfg.audio.bitrate, 40000);
    EXPECT_EQ(cfg.audio.mode, InputMode::VoiceActivity);
    EXPECT_EQ(cfg.shortcuts.value(QStringLiteral("quick_switcher")), QStringLiteral("Ctrl+K"));
}

TEST(ClientConfig, ParsesAndClamps)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("config.toml"));
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(R"(
[audio]
mode = "ptt"
bitrate = 500000
noise_suppression = false
[ui]
compact_mode = false
[shortcuts]
quick_switcher = "Ctrl+P"
)");
    f.close();
    QString error;
    const auto cfg = ClientConfig::load(path, &error);
    EXPECT_TRUE(error.isEmpty()) << error.toStdString();
    EXPECT_EQ(cfg.audio.mode, InputMode::PushToTalk);
    EXPECT_EQ(cfg.audio.bitrate, 96000) << "bitrate is clamped to 24-96 kbps";
    EXPECT_FALSE(cfg.audio.noiseSuppression);
    EXPECT_FALSE(cfg.ui.compactMode);
    EXPECT_EQ(cfg.shortcuts.value(QStringLiteral("quick_switcher")), QStringLiteral("Ctrl+P"));
    EXPECT_EQ(cfg.shortcuts.value(QStringLiteral("toggle_mute")), QStringLiteral("Ctrl+Shift+M"));
}

TEST(ClientConfig, SyntaxErrorReportedWithDefaults)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("config.toml"));
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("[audio\nmode = ");
    f.close();
    QString error;
    const auto cfg = ClientConfig::load(path, &error);
    EXPECT_FALSE(error.isEmpty());
    EXPECT_EQ(cfg.audio.bitrate, 40000);
}

TEST(ClientConfig, SaveLoadRoundTrip)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("sub/config.toml"));
    ClientConfig cfg = ClientConfig::load(path);
    cfg.audio.input = QStringLiteral("alsa_input.usb-mic");
    cfg.audio.mode = InputMode::AlwaysTransmit;
    cfg.notifications.voiceJoin = true;
    cfg.shortcuts.insert(QStringLiteral("toggle_mute"), QStringLiteral("F9"));
    QString error;
    ASSERT_TRUE(cfg.save(path, &error)) << error.toStdString();
    const auto back = ClientConfig::load(path, &error);
    EXPECT_EQ(back.audio.input, cfg.audio.input);
    EXPECT_EQ(back.audio.mode, InputMode::AlwaysTransmit);
    EXPECT_TRUE(back.notifications.voiceJoin);
    EXPECT_EQ(back.shortcuts.value(QStringLiteral("toggle_mute")), QStringLiteral("F9"));
}
