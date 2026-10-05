#include "platform/ThemeProvider.hpp"

#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>

#include <toml++/toml.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace omachat::client {

namespace {
ThemeProvider* g_instance = nullptr;

double luminance(const QColor& c)
{
    auto ch = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
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

// Keep the original hue where possible, choosing the endpoint that can meet
// the target on every surface. Small steps avoid unnecessary palette shifts.
QColor readableOn(QColor fg, std::initializer_list<QColor> backgrounds, double ratio = 4.5)
{
    auto minimumContrast = [&](const QColor& color) {
        double result = 21.0;
        for (const auto& bg : backgrounds)
            result = std::min(result, contrast(color, bg));
        return result;
    };
    const QColor black(Qt::black), white(Qt::white);
    const QColor target = minimumContrast(white) > minimumContrast(black) ? white : black;
    const QColor original = fg;
    for (int i = 1; i <= 100 && minimumContrast(fg) < ratio; ++i)
        fg = mix(original, target, i / 100.0);
    return fg;
}

QColor readable(QColor fg, const QColor& bg, double ratio = 4.5)
{
    return readableOn(fg, {bg}, ratio);
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
    return readableOn(c, {m_background, m_surface, m_surfaceAlt, m_raised, m_selection});
}

QColor ThemeProvider::contrastingText(const QColor& background) const
{
    const QColor black(0x10, 0x10, 0x10), white(Qt::white);
    return contrast(black, background) > contrast(white, background) ? black : white;
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
    // A selection must stay on the same luminance side as the surrounding UI:
    // one shared text token then remains readable during hover and selection.
    // Preserve the supplied color unless its luminance makes that impossible.
    const QColor selectionTarget = m_dark ? QColor(Qt::white) : QColor(Qt::black);
    const QColor originalSelection = m_selection;
    for (int i = 1; i <= 100 && contrast(selectionTarget, m_selection) < 7.0; ++i)
        m_selection = mix(originalSelection, bg, i / 100.0);
    const auto surfaces = {m_background, m_surface, m_surfaceAlt, m_raised, m_selection};
    m_text = readableOn(fg, surfaces);
    m_textMuted = readableOn(mix(fg, bg, 0.35), surfaces);
    // This token labels placeholders and small descriptions, not decoration.
    m_textFaint = readableOn(mix(fg, bg, 0.55), surfaces);
    m_accent = readableOn(accent, surfaces);
    m_accentText = contrastingText(m_accent);
    m_accentHover = readable(mix(m_accent, toward, 0.10), m_accentText);
    m_accentPressed = readable(mix(m_accent, bg, 0.12), m_accentText);
    m_danger = readableOn(red, surfaces);
    m_success = readableOn(green, surfaces);
    m_warning = readableOn(yellow, surfaces);
    m_controlBorder = readableOn(m_border, surfaces, 3.0);
    m_focus = readableOn(accent, surfaces, 3.0);
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
        m_success
            = readableOn(QColor(0x5f, 0xc2, 0x7a), {m_background, m_surface, m_surfaceAlt, m_raised, m_selection});
    if (std::abs(m_warning.hueF() - 0.12) > 0.1)
        m_warning = m_idle
            = readableOn(QColor(0xe5, 0xb5, 0x4a), {m_background, m_surface, m_surfaceAlt, m_raised, m_selection});
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
