#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

namespace omachat::config {

enum class InputMode { VoiceActivity, PushToTalk, AlwaysTransmit };

QString toString(InputMode mode);
InputMode inputModeFromString(const QString& s, InputMode fallback = InputMode::VoiceActivity);

// $XDG_CONFIG_HOME/omachat/config.toml. Never contains secrets.
struct ClientConfig {
    struct Startup {
        bool launchDaemon = true;
        bool openOnLogin = false;
    } startup;

    struct Audio {
        QString input = QStringLiteral("default");
        QString output = QStringLiteral("default");
        InputMode mode = InputMode::VoiceActivity;
        bool noiseSuppression = true;
        bool echoCancellation = false;
        bool highPass = true;
        bool automaticGain = false;
        double vadThresholdDb = -50.0; // dBFS
        int vadHangoverMs = 300;
        int bitrate = 40000; // 24000..96000
        bool fec = true;
        int jitterMinMs = 20;
        int jitterMaxMs = 200;
        double inputVolume = 1.0; // 0..2
        double outputVolume = 1.0; // 0..2
    } audio;

    // Screen sharing (what you send; viewers get whatever the sender picked).
    struct Video {
        int fps = 30; // 5..60
        int maxHeight = 1080; // 360..1440; width follows the aspect ratio
        int bitrateKbps = 4000; // 500..20000
        QString encoder = QStringLiteral("auto"); // auto | nvenc | amf | x264
        bool audio = true; // share what other applications play (never OmaChat's own sound)
    } video;

    struct Notifications {
        bool messages = true;
        bool mentions = true;
        bool voiceJoin = false;
    } notifications;

    struct Ui {
        bool compactMode = true;
        double scale = 1.0;
        bool reducedMotion = false;
        QString theme = QStringLiteral("auto"); // auto | omarchy | dark | light
    } ui;

    QString logLevel = QStringLiteral("info");

    // Action name -> key sequence (Qt portable text, e.g. "Ctrl+K").
    QMap<QString, QString> shortcuts;

    static QMap<QString, QString> defaultShortcuts();

    // Loads the file, applying defaults for missing keys. Parse errors are
    // reported through `error` and the defaults are returned.
    static ClientConfig load(const QString& path, QString* error = nullptr);

    // Writes the whole config atomically (temp file + rename).
    bool save(const QString& path, QString* error = nullptr) const;
};

} // namespace omachat::config
