#include "omachat/config/ClientConfig.hpp"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

#include <toml++/toml.hpp>

#include <algorithm>
#include <sstream>

namespace omachat::config {

QString toString(InputMode mode)
{
    switch (mode) {
    case InputMode::VoiceActivity:
        return QStringLiteral("vad");
    case InputMode::PushToTalk:
        return QStringLiteral("ptt");
    case InputMode::AlwaysTransmit:
        return QStringLiteral("always");
    }
    return QStringLiteral("vad");
}

InputMode inputModeFromString(const QString& s, InputMode fallback)
{
    if (s == u"vad")
        return InputMode::VoiceActivity;
    if (s == u"ptt")
        return InputMode::PushToTalk;
    if (s == u"always")
        return InputMode::AlwaysTransmit;
    return fallback;
}

QMap<QString, QString> ClientConfig::defaultShortcuts()
{
    return {
        {QStringLiteral("quick_switcher"), QStringLiteral("Ctrl+K")},
        {QStringLiteral("toggle_mute"), QStringLiteral("Ctrl+Shift+M")},
        {QStringLiteral("toggle_deafen"), QStringLiteral("Ctrl+Shift+D")},
        {QStringLiteral("previous_channel"), QStringLiteral("Alt+Up")},
        {QStringLiteral("next_channel"), QStringLiteral("Alt+Down")},
        {QStringLiteral("focus_channels"), QStringLiteral("Ctrl+L")},
        {QStringLiteral("search"), QStringLiteral("Ctrl+F")},
        {QStringLiteral("command_help"), QStringLiteral("Ctrl+/")},
        {QStringLiteral("push_to_talk"), QStringLiteral("F8")},
    };
}

namespace {

QString str(const toml::node_view<const toml::node>& n, const QString& fallback)
{
    if (auto v = n.value<std::string>())
        return QString::fromStdString(*v);
    return fallback;
}

template <typename T> T num(const toml::node_view<const toml::node>& n, T fallback, T lo, T hi)
{
    if (auto v = n.value<double>())
        return std::clamp(static_cast<T>(*v), lo, hi);
    return fallback;
}

bool flag(const toml::node_view<const toml::node>& n, bool fallback)
{
    return n.value<bool>().value_or(fallback);
}

} // namespace

ClientConfig ClientConfig::load(const QString& path, QString* error)
{
    ClientConfig cfg;
    cfg.shortcuts = defaultShortcuts();
    if (!QFileInfo::exists(path))
        return cfg;

    toml::table tbl;
    try {
        tbl = toml::parse_file(path.toStdString());
    } catch (const toml::parse_error& e) {
        if (error) {
            std::ostringstream os;
            os << e;
            *error = QString::fromStdString(os.str());
        }
        return cfg;
    }
    const toml::table& t = tbl;

    cfg.startup.launchDaemon = flag(t["startup"]["launch_daemon"], cfg.startup.launchDaemon);
    cfg.startup.openOnLogin = flag(t["startup"]["open_on_login"], cfg.startup.openOnLogin);

    auto a = t["audio"];
    cfg.audio.input = str(a["input"], cfg.audio.input);
    cfg.audio.output = str(a["output"], cfg.audio.output);
    cfg.audio.mode = inputModeFromString(str(a["mode"], toString(cfg.audio.mode)), cfg.audio.mode);
    cfg.audio.noiseSuppression = flag(a["noise_suppression"], cfg.audio.noiseSuppression);
    cfg.audio.echoCancellation = flag(a["echo_cancellation"], cfg.audio.echoCancellation);
    cfg.audio.highPass = flag(a["high_pass"], cfg.audio.highPass);
    cfg.audio.automaticGain = flag(a["automatic_gain"], cfg.audio.automaticGain);
    cfg.audio.vadThresholdDb = num(a["vad_threshold_db"], cfg.audio.vadThresholdDb, -90.0, 0.0);
    cfg.audio.vadHangoverMs = num(a["vad_hangover_ms"], cfg.audio.vadHangoverMs, 0, 2000);
    cfg.audio.bitrate = num(a["bitrate"], cfg.audio.bitrate, 24000, 96000);
    cfg.audio.fec = flag(a["fec"], cfg.audio.fec);
    cfg.audio.jitterMinMs = num(a["jitter_min_ms"], cfg.audio.jitterMinMs, 20, 200);
    cfg.audio.jitterMaxMs = num(a["jitter_max_ms"], cfg.audio.jitterMaxMs, cfg.audio.jitterMinMs, 1000);
    cfg.audio.inputVolume = num(a["input_volume"], cfg.audio.inputVolume, 0.0, 2.0);
    cfg.audio.outputVolume = num(a["output_volume"], cfg.audio.outputVolume, 0.0, 2.0);

    auto v = t["video"];
    cfg.video.fps = num(v["fps"], cfg.video.fps, 5, 60);
    cfg.video.maxHeight = num(v["max_height"], cfg.video.maxHeight, 360, 1440);
    cfg.video.bitrateKbps = num(v["bitrate_kbps"], cfg.video.bitrateKbps, 500, 20000);
    cfg.video.encoder = str(v["encoder"], cfg.video.encoder);

    auto n = t["notifications"];
    cfg.notifications.messages = flag(n["messages"], cfg.notifications.messages);
    cfg.notifications.mentions = flag(n["mentions"], cfg.notifications.mentions);
    cfg.notifications.voiceJoin = flag(n["voice_join"], cfg.notifications.voiceJoin);

    auto u = t["ui"];
    cfg.ui.compactMode = flag(u["compact_mode"], cfg.ui.compactMode);
    cfg.ui.scale = num(u["scale"], cfg.ui.scale, 0.5, 3.0);
    cfg.ui.reducedMotion = flag(u["reduced_motion"], cfg.ui.reducedMotion);
    cfg.ui.theme = str(u["theme"], cfg.ui.theme);

    cfg.logLevel = str(t["log"]["level"], cfg.logLevel);

    if (auto sc = t["shortcuts"].as_table()) {
        for (const auto& [key, value] : *sc) {
            if (auto s = value.value<std::string>())
                cfg.shortcuts.insert(QString::fromUtf8(key.str().data(), static_cast<qsizetype>(key.str().size())),
                    QString::fromStdString(*s));
        }
    }
    return cfg;
}

bool ClientConfig::save(const QString& path, QString* error) const
{
    toml::table root;
    root.insert(
        "startup", toml::table{{"launch_daemon", startup.launchDaemon}, {"open_on_login", startup.openOnLogin}});
    root.insert("audio",
        toml::table{
            {"input", audio.input.toStdString()},
            {"output", audio.output.toStdString()},
            {"mode", toString(audio.mode).toStdString()},
            {"noise_suppression", audio.noiseSuppression},
            {"echo_cancellation", audio.echoCancellation},
            {"high_pass", audio.highPass},
            {"automatic_gain", audio.automaticGain},
            {"vad_threshold_db", audio.vadThresholdDb},
            {"vad_hangover_ms", audio.vadHangoverMs},
            {"bitrate", audio.bitrate},
            {"fec", audio.fec},
            {"jitter_min_ms", audio.jitterMinMs},
            {"jitter_max_ms", audio.jitterMaxMs},
            {"input_volume", audio.inputVolume},
            {"output_volume", audio.outputVolume},
        });
    root.insert("video",
        toml::table{{"fps", video.fps}, {"max_height", video.maxHeight}, {"bitrate_kbps", video.bitrateKbps},
            {"encoder", video.encoder.toStdString()}});
    root.insert("notifications",
        toml::table{{"messages", notifications.messages}, {"mentions", notifications.mentions},
            {"voice_join", notifications.voiceJoin}});
    root.insert("ui",
        toml::table{{"compact_mode", ui.compactMode}, {"scale", ui.scale}, {"reduced_motion", ui.reducedMotion},
            {"theme", ui.theme.toStdString()}});
    root.insert("log", toml::table{{"level", logLevel.toStdString()}});
    toml::table sc;
    for (auto it = shortcuts.cbegin(); it != shortcuts.cend(); ++it)
        sc.insert(it.key().toStdString(), it.value().toStdString());
    root.insert("shortcuts", std::move(sc));

    std::ostringstream os;
    os << "# OmaChat client configuration. Secrets are never stored here.\n" << root << '\n';

    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    const std::string data = os.str();
    file.write(data.data(), static_cast<qint64>(data.size()));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace omachat::config
