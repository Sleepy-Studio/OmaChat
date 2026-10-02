#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

namespace omachat::client {

// OmaChat's single source of colors and metrics for QML.
//
//   Omarchy theme (colors.toml) -> ThemeProvider -> Theme.* bindings in QML
//
// No QML file reads Omarchy paths directly. Outside Omarchy (or when the
// theme file is missing or unreadable) a built-in dark palette is used.
// Theme switches are picked up live through file-system notifications.
class ThemeProvider : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Theme)
    QML_SINGLETON

    Q_PROPERTY(QString source READ source NOTIFY changed)
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(QColor background READ background NOTIFY changed)
    Q_PROPERTY(QColor surface READ surface NOTIFY changed) // sidebars
    Q_PROPERTY(QColor surfaceAlt READ surfaceAlt NOTIFY changed) // server rail, inputs
    Q_PROPERTY(QColor raised READ raised NOTIFY changed) // popups, hover
    Q_PROPERTY(QColor selection READ selection NOTIFY changed)
    Q_PROPERTY(QColor border READ border NOTIFY changed)
    Q_PROPERTY(QColor text READ text NOTIFY changed)
    Q_PROPERTY(QColor textMuted READ textMuted NOTIFY changed)
    Q_PROPERTY(QColor textFaint READ textFaint NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor accentText READ accentText NOTIFY changed)
    Q_PROPERTY(QColor danger READ danger NOTIFY changed)
    Q_PROPERTY(QColor success READ success NOTIFY changed)
    Q_PROPERTY(QColor warning READ warning NOTIFY changed)
    Q_PROPERTY(QColor idle READ idle NOTIFY changed)
    Q_PROPERTY(QColor mention READ mention NOTIFY changed)
    Q_PROPERTY(QColor codeBackground READ codeBackground NOTIFY changed)
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY changed)
    Q_PROPERTY(QString monoFamily READ monoFamily NOTIFY changed)
    Q_PROPERTY(double scale READ scale WRITE setScale NOTIFY changed)
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY changed)
    Q_PROPERTY(int animationMs READ animationMs NOTIFY changed)

public:
    // Require an explicit parent so QML uses create(), rather than making a
    // second default-constructed singleton that discards startup preferences.
    explicit ThemeProvider(QObject* parent);

    static ThemeProvider* instance();
    static ThemeProvider* create(QQmlEngine*, QJSEngine*);

    QString source() const { return m_source; }
    bool dark() const { return m_dark; }
    QColor background() const { return m_background; }
    QColor surface() const { return m_surface; }
    QColor surfaceAlt() const { return m_surfaceAlt; }
    QColor raised() const { return m_raised; }
    QColor selection() const { return m_selection; }
    QColor border() const { return m_border; }
    QColor text() const { return m_text; }
    QColor textMuted() const { return m_textMuted; }
    QColor textFaint() const { return m_textFaint; }
    QColor accent() const { return m_accent; }
    QColor accentText() const { return m_accentText; }
    QColor danger() const { return m_danger; }
    QColor success() const { return m_success; }
    QColor warning() const { return m_warning; }
    QColor idle() const { return m_idle; }
    QColor mention() const { return m_mention; }
    QColor codeBackground() const { return m_codeBackground; }
    QString fontFamily() const { return m_fontFamily; }
    QString monoFamily() const { return m_monoFamily; }
    double scale() const { return m_scale; }
    void setScale(double s);
    bool reducedMotion() const { return m_reducedMotion; }
    void setReducedMotion(bool r);
    int animationMs() const { return m_reducedMotion ? 0 : 120; }

    // Deterministic per-user accent for avatars/names without role color.
    Q_INVOKABLE QColor userColor(const QString& id) const;

    // Loads a specific colors.toml (tests / explicit override).
    bool loadFile(const QString& path);

signals:
    void changed();

private:
    void reload();
    void applyFallback();
    void derive(const QColor& bg, const QColor& fg, const QColor& accent, const QColor& selection, const QColor& red,
        const QColor& green, const QColor& yellow);
    QString themeDirectory() const;

    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_source = QStringLiteral("builtin");
    bool m_dark = true;
    QColor m_background, m_surface, m_surfaceAlt, m_raised, m_selection, m_border;
    QColor m_text, m_textMuted, m_textFaint, m_accent, m_accentText;
    QColor m_danger, m_success, m_warning, m_idle, m_mention, m_codeBackground;
    QString m_fontFamily;
    QString m_monoFamily;
    double m_scale = 1.0;
    bool m_reducedMotion = false;
};

} // namespace omachat::client
