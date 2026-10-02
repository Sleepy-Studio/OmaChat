#include "platform/ThemeProvider.hpp"

#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>

#include <toml++/toml.hpp>

#include <algorithm>

namespace omachat::client {

namespace {
ThemeProvider* g_instance = nullptr;

double luminance(const QColor& c)
{
    auto ch = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * ch(c.redF()) + 0.7152 * ch(c.greenF()) + 0.0722 * ch(c.blueF());
}

double contrast(const QColor& a, const QColor& b)
{
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

QColor mix(const QColor& a, const QColor& b, double t)
{
    return QColor::fromRgbF(static_cast<float>(a.redF() * (1 - t) + b.redF() * t),
        static_cast<float>(a.greenF() * (1 - t) + b.greenF() * t),
        static_cast<float>(a.blueF() * (1 - t) + b.blueF() * t));
}

// Ensures readable text: nudges `fg` toward white/black until it reaches the
// WCAG AA ratio against `bg`.
QColor readable(QColor fg, const QColor& bg, double ratio = 4.5)
{
    const QColor target = luminance(bg) < 0.5 ? QColor(Qt::white) : QColor(Qt::black);
    for (int i = 0; i < 20 && contrast(fg, bg) < ratio; ++i)
        fg = mix(fg, target, 0.15);
    return fg;
}

QString pickFont(const QStringList& candidates, QFontDatabase::SystemFont fallback)
{
    const QStringList families = QFontDatabase::families();
    for (const auto& c : candidates) {
        if (families.contains(c))
            return c;
    }
    return QFontDatabase::systemFont(fallback).family();
}

} // namespace

ThemeProvider::ThemeProvider(QObject* parent)
    : QObject(parent)
{
    g_instance = this;
    m_fontFamily = QGuiApplication::font().family();
    m_monoFamily = pickFont({QStringLiteral("JetBrainsMono Nerd Font"), QStringLiteral("JetBrains Mono"),
                                QStringLiteral("CaskaydiaMono Nerd Font"), QStringLiteral("DejaVu Sans Mono")},
        QFontDatabase::FixedFont);
    applyFallback();

    // Theme switches rewrite files in quick succession; coalesce them.
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(150);
    connect(&m_debounce, &QTimer::timeout, this, &ThemeProvider::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    if (qEnvironmentVariable("OMACHAT_THEME") != u"builtin")
        reload();
}

ThemeProvider* ThemeProvider::instance()
{
    return g_instance;
}

ThemeProvider* ThemeProvider::create(QQmlEngine*, QJSEngine*)
{
    // Owned by main(); QML must not delete it.
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

QString ThemeProvider::themeDirectory() const
{
    QString state = qEnvironmentVariable("XDG_STATE_HOME");
    if (state.isEmpty() || !QDir::isAbsolutePath(state))
        state = QDir::homePath() + QStringLiteral("/.local/state");
    return state + QStringLiteral("/omarchy/current/theme");
}

void ThemeProvider::setScale(double s)
{
    s = std::clamp(s, 0.5, 3.0);
    if (qFuzzyCompare(s, m_scale))
        return;
    m_scale = s;
    emit changed();
}

void ThemeProvider::setReducedMotion(bool r)
{
    if (r == m_reducedMotion)
        return;
    m_reducedMotion = r;
    emit changed();
}

QColor ThemeProvider::userColor(const QString& id) const
{
    const auto h = static_cast<int>(qHash(id) % 360);
    QColor c = QColor::fromHsl(h, m_dark ? 140 : 170, m_dark ? 165 : 90);
    return readable(c, m_background, 3.0);
}

void ThemeProvider::applyFallback()
{
    m_source = QStringLiteral("builtin");
    derive(QColor(0x12, 0x14, 0x16), QColor(0xd0, 0xd3, 0xd6), QColor(0x5b, 0x9d, 0xf5), QColor(0x2a, 0x2f, 0x36),
        QColor(0xe0, 0x5d, 0x5d), QColor(0x5f, 0xc2, 0x7a), QColor(0xe5, 0xb5, 0x4a));
}

void ThemeProvider::derive(const QColor& bg, const QColor& fg, const QColor& accent, const QColor& selection,
    const QColor& red, const QColor& green, const QColor& yellow)
{
    m_dark = luminance(bg) < 0.4;
    const QColor toward = m_dark ? QColor(Qt::white) : QColor(Qt::black);
    m_background = bg;
    m_surface = mix(bg, toward, m_dark ? 0.035 : 0.03);
    m_surfaceAlt = mix(bg, m_dark ? QColor(Qt::black) : QColor(Qt::white), 0.25);
    m_raised = mix(bg, toward, m_dark ? 0.08 : 0.06);
    m_selection = selection.isValid() ? selection : mix(bg, accent, 0.25);
    m_border = mix(bg, toward, m_dark ? 0.12 : 0.15);
    m_text = readable(fg, m_surface);
    m_textMuted = readable(mix(fg, bg, 0.35), m_raised, 4.5);
    m_textFaint = readable(mix(fg, bg, 0.55), m_surface, 3.0);
    m_accent = readable(accent, m_surface, 3.0);
    m_accentText = luminance(m_accent) > 0.4 ? QColor(0x10, 0x10, 0x10) : QColor(Qt::white);
    m_danger = readable(red, m_surface, 3.0);
    m_success = readable(green, m_surface, 3.0);
    m_warning = readable(yellow, m_surface, 3.0);
    m_idle = m_warning;
    m_mention = m_accent;
    m_codeBackground = mix(bg, m_dark ? QColor(Qt::black) : QColor(Qt::white), 0.35);
}

bool ThemeProvider::loadFile(const QString& path)
{
    toml::table t;
    try {
        t = toml::parse_file(path.toStdString());
    } catch (const toml::parse_error&) {
        return false;
    }
    auto color = [&](const char* key, const QColor& fallback) {
        if (auto v = t[key].value<std::string>()) {
            const QColor c(QString::fromStdString(*v));
            if (c.isValid())
                return c;
        }
        return fallback;
    };
    const QColor bg = color("background", QColor());
    const QColor fg = color("foreground", QColor());
    if (!bg.isValid() || !fg.isValid())
        return false;
    const QColor accent = color("accent", color("blue", fg));
    derive(bg, fg, accent, color("selection", QColor()), color("red", QColor(0xe0, 0x5d, 0x5d)),
        color("green", QColor(0x5f, 0xc2, 0x7a)), color("yellow", QColor(0xe5, 0xb5, 0x4a)));
    // Omarchy's green/yellow slots are not always green/yellow; keep presence
    // semantics recognizable when a theme repurposes them.
    if (std::abs(m_success.hueF() - 0.33) > 0.12)
        m_success = readable(QColor(0x5f, 0xc2, 0x7a), m_surface, 3.0);
    if (std::abs(m_warning.hueF() - 0.12) > 0.1)
        m_warning = m_idle = readable(QColor(0xe5, 0xb5, 0x4a), m_surface, 3.0);
    m_source = path;
    return true;
}

void ThemeProvider::reload()
{
    const QString dir = themeDirectory();
    const QString colors = dir + QStringLiteral("/colors.toml");
    const QString current = QFileInfo(dir).absolutePath(); // .../omarchy/current

    if (!QFileInfo::exists(colors) || !loadFile(colors))
        applyFallback();

    // Re-arm watches: theme switches may replace files and directories.
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    for (const QString& p : {colors, current + QStringLiteral("/theme.name")}) {
        if (QFileInfo::exists(p))
            m_watcher.addPath(p);
    }
    if (QFileInfo::exists(current))
        m_watcher.addPath(current);
    emit changed();
}

} // namespace omachat::client
