#include "platform/ThemeProvider.hpp"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <memory>

using omachat::client::ThemeProvider;

namespace {
double luminance(const QColor& color)
{
    const auto linear = [](double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
}
double contrast(const QColor& a, const QColor& b)
{
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}
struct Palette {
    const char* name;
    const char* background;
    const char* foreground;
    const char* accent;
    const char* selection;
    const char* red;
    const char* green;
    const char* yellow;
};
// Snapshots of locally installed Omarchy palettes, inspected 2026-10-04.
// Keep these portable: CI need not have Omarchy or a particular active theme.
constexpr Palette palettes[] = {
    {"tokyo-night", "#1a1b26", "#a9b1d6", "#7aa2f7", "#292e42", "#f7768e", "#9ece6a", "#e0af68"},
    {"catppuccin", "#1e1e2e", "#cdd6f4", "#89b4fa", "#45475a", "#f38ba8", "#a6e3a1", "#f9e2af"},
    {"gruvbox", "#282828", "#d4be98", "#7daea3", "#504945", "#ea6962", "#a9b665", "#d8a657"},
    {"everforest", "#2d353b", "#d3c6aa", "#7fbbb3", "#3d484d", "#e67e80", "#a7c080", "#dbbc7f"},
    {"white", "#ffffff", "#000000", "#6e6e6e", "#c0c0c0", "#2a2a2a", "#3a3a3a", "#4a4a4a"},
    {"lupine", "#fafafa", "#212121", "#3264eb", "#d0d0d0", "#c900c4", "#4a2fd0", "#026fde"},
    {"rose-pine", "#faf4ed", "#575279", "#56949f", "#dfdad9", "#b4637a", "#286983", "#ea9d34"},
    {"opposite-selection-dark", "#121416", "#d0d3d6", "#5b9df5", "#eeeeee", "#e05d5d", "#5fc27a", "#e5b54a"},
    {"opposite-selection-light", "#fafafa", "#262626", "#2563eb", "#151515", "#dc2626", "#15803d", "#ca8a04"},
};

bool load(ThemeProvider& theme, const Palette& palette)
{
    QTemporaryDir dir;
    QFile file(dir.filePath("colors.toml"));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const auto entry
        = [&](const char* key, const char* value) { file.write(QByteArray(key) + " = '" + value + "'\n"); };
    entry("background", palette.background);
    entry("foreground", palette.foreground);
    entry("accent", palette.accent);
    entry("selection", palette.selection);
    entry("red", palette.red);
    entry("green", palette.green);
    entry("yellow", palette.yellow);
    file.close();
    return theme.loadFile(file.fileName());
}

void checkPalette(const ThemeProvider& theme)
{
    for (const QColor& surface :
        {theme.background(), theme.surface(), theme.surfaceAlt(), theme.raised(), theme.selection()}) {
        for (const QColor& text : {theme.text(), theme.textMuted(), theme.textFaint(), theme.accent(), theme.danger(),
                 theme.success(), theme.warning()})
            EXPECT_GE(contrast(text, surface), 4.5)
                << text.name().toStdString() << " on " << surface.name().toStdString();
        EXPECT_GE(contrast(theme.focus(), surface), 3.0);
        EXPECT_GE(contrast(theme.controlBorder(), surface), 3.0);
        for (int i = 0; i < 360; ++i)
            EXPECT_GE(contrast(theme.userColor(QString::number(i)), surface), 4.5);
    }
    for (const QColor& fill : {theme.accent(), theme.accentHover(), theme.accentPressed()})
        EXPECT_GE(contrast(theme.accentText(), fill), 4.5);
    EXPECT_GE(contrast(theme.dangerText(), theme.danger()), 4.5);
    EXPECT_GE(contrast(theme.successText(), theme.success()), 4.5);
}
QQuickItem* itemProperty(QObject* object, const char* name)
{
    return qobject_cast<QQuickItem*>(object->property(name).value<QObject*>());
}
} // namespace

TEST(ThemeStates, InstalledPaletteSnapshotsKeepTextAndActionCuesReadable)
{
    ThemeProvider theme(nullptr);
    checkPalette(theme);
    for (const auto& palette : palettes) {
        SCOPED_TRACE(palette.name);
        ASSERT_TRUE(load(theme, palette));
        EXPECT_EQ(theme.background(), QColor(palette.background));
        checkPalette(theme);
    }
    // Optional live audit supplements the portable matrix without requiring
    // host Omarchy files in CI or mutating the user's current palette.
    const QString installed = qEnvironmentVariable("OMACHAT_THEME_AUDIT_DIR");
    if (!installed.isEmpty()) {
        const QDir directory(installed);
        const auto names = directory.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        ASSERT_FALSE(names.isEmpty());
        for (const auto& name : names) {
            SCOPED_TRACE(name.toStdString());
            const QString path = directory.filePath(name + "/colors.toml");
            if (!QFile::exists(path))
                continue;
            ASSERT_TRUE(theme.loadFile(path));
            checkPalette(theme);
        }
    }
}

TEST(ThemeStates, ProductionPrimaryButtonUsesReadablePressedAndHoverFills)
{
    ThemeProvider theme(nullptr);
    for (const auto& palette : palettes) {
        SCOPED_TRACE(palette.name);
        ASSERT_TRUE(load(theme, palette));
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(
            "import QtQuick\nimport OmaChat\nFlatButton { text: 'Save'; primary: true; width: 100; height: 40 }",
            QUrl());
        std::unique_ptr<QObject> object(component.create());
        ASSERT_TRUE(object) << component.errorString().toStdString();
        auto* button = qobject_cast<QQuickItem*>(object.get());
        ASSERT_TRUE(button);
        QQuickWindow window;
        window.resize(220, 100);
        button->setParentItem(window.contentItem());
        window.show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
        auto* label = itemProperty(button, "contentItem");
        auto* background = itemProperty(button, "background");
        ASSERT_TRUE(label);
        ASSERT_TRUE(background);
        const auto assertState = [&] {
            EXPECT_GE(
                contrast(label->property("color").value<QColor>(), background->property("color").value<QColor>()), 4.5);
        };
        QTest::mouseMove(&window, QPoint(170, 70));
        QCoreApplication::processEvents();
        assertState();
        QTest::mouseMove(&window, QPoint(50, 20));
        QCoreApplication::processEvents();
        ASSERT_TRUE(button->property("hovered").toBool());
        EXPECT_EQ(background->property("color").value<QColor>(), theme.accentHover());
        assertState();
        button->setProperty("down", true);
        EXPECT_EQ(background->property("color").value<QColor>(), theme.accentPressed());
        assertState();
        button->setProperty("down", false);
        button->forceActiveFocus(Qt::TabFocusReason);
        QCoreApplication::processEvents();
        EXPECT_TRUE(button->property("visualFocus").toBool());
        button->setEnabled(false);
        // Inactive controls are deliberately dimmed and exempt from AA text;
        // their disabled state must still block activation.
        EXPECT_LT(label->opacity(), 1.0);
        EXPECT_FALSE(button->isEnabled());
        button->setParentItem(nullptr);
    }
}

TEST(ThemeStates, ProductionPermissionSegmentsSupportKeyboardAndCorrectFillText)
{
    ThemeProvider theme(nullptr);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nimport OmaChat\nTriState { width: 84; height: 22 }", QUrl());
    std::unique_ptr<QObject> object(component.create());
    ASSERT_TRUE(object) << component.errorString().toStdString();
    auto* root = qobject_cast<QQuickItem*>(object.get());
    ASSERT_TRUE(root);
    QQuickWindow window;
    window.resize(200, 80);
    root->setParentItem(window.contentItem());
    window.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    QList<QQuickItem*> segments;
    for (auto* child : root->childItems())
        if (child->property("modelData").isValid())
            segments.append(child);
    ASSERT_EQ(segments.size(), 3);
    segments[0]->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(&window, Qt::Key_Space);
    EXPECT_EQ(root->property("value").toInt(), -1);
    QTest::keyClick(&window, Qt::Key_Right);
    EXPECT_TRUE(segments[1]->hasActiveFocus());
    QTest::keyClick(&window, Qt::Key_Return);
    EXPECT_EQ(root->property("value").toInt(), 0);
    QTest::keyClick(&window, Qt::Key_Right);
    QTest::keyClick(&window, Qt::Key_Return);
    EXPECT_EQ(root->property("value").toInt(), 1);
    for (const auto& palette : palettes) {
        SCOPED_TRACE(palette.name);
        ASSERT_TRUE(load(theme, palette));
        // loadFile is an explicit test override; notify existing QML bindings.
        QMetaObject::invokeMethod(&theme, "changed");
        for (int i = 0; i < 3; ++i) {
            root->setProperty("value", i - 1);
            for (auto* child : segments[i]->childItems()) {
                if (!child->property("text").isValid())
                    continue;
                EXPECT_GE(
                    contrast(child->property("color").value<QColor>(), segments[i]->property("color").value<QColor>()),
                    4.5);
            }
        }
    }
    root->setEnabled(false);
    QTest::keyClick(&window, Qt::Key_Left);
    QTest::keyClick(&window, Qt::Key_Space);
    EXPECT_EQ(root->property("value").toInt(), 1);
    root->setParentItem(nullptr);
}

TEST(ThemeStates, ProductionInputCheckboxAndBadgeUseTheirSemanticTokens)
{
    ThemeProvider theme(nullptr);
    for (const auto& palette : palettes) {
        SCOPED_TRACE(palette.name);
        ASSERT_TRUE(load(theme, palette));
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import OmaChat
Item {
    width: 300; height: 180
    Field { objectName: "field"; width: 200; label: "Name"; hint: "Choose a name"; placeholder: "Enter name" }
    Check { objectName: "check"; y: 90; text: "Enable"; description: "Optional setting"; checked: true }
    Badge { objectName: "badge"; y: 145; count: 3 }
}
)",
            QUrl());
        std::unique_ptr<QObject> object(component.create());
        ASSERT_TRUE(object) << component.errorString().toStdString();
        auto* field = object->findChild<QObject*>("field");
        auto* check = object->findChild<QObject*>("check");
        auto* badge = object->findChild<QQuickItem*>("badge");
        ASSERT_TRUE(field);
        ASSERT_TRUE(check);
        ASSERT_TRUE(badge);
        auto* input = itemProperty(field, "input");
        ASSERT_TRUE(input);
        auto* background = itemProperty(input, "background");
        ASSERT_TRUE(background);
        EXPECT_GE(contrast(input->property("placeholderTextColor").value<QColor>(),
                      background->property("color").value<QColor>()),
            4.5);
        EXPECT_GE(contrast(input->property("selectedTextColor").value<QColor>(),
                      input->property("selectionColor").value<QColor>()),
            4.5);
        auto* indicator = itemProperty(check, "indicator");
        ASSERT_TRUE(indicator);
        ASSERT_FALSE(indicator->childItems().isEmpty());
        for (auto* child : indicator->childItems())
            if (child->property("text").isValid())
                EXPECT_GE(
                    contrast(child->property("color").value<QColor>(), indicator->property("color").value<QColor>()),
                    4.5);
        for (auto* child : badge->childItems())
            if (child->property("text").isValid())
                EXPECT_GE(
                    contrast(child->property("color").value<QColor>(), badge->property("color").value<QColor>()), 4.5);
        check->setProperty("enabled", false);
        EXPECT_LT(indicator->opacity(), 1.0);
    }
}

int main(int argc, char** argv)
{
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("OMACHAT_THEME", "builtin");
    QGuiApplication app(argc, argv);
    qmlRegisterSingletonType<ThemeProvider>("OmaChat", 1, 0, "Theme", &ThemeProvider::create);
    const QString qml = QStringLiteral(OMACHAT_SOURCE_DIR "/client/qml/");
    qmlRegisterSingletonType(QUrl::fromLocalFile(qml + "Metrics.qml"), "OmaChat", 1, 0, "Metrics");
    for (const QString& control : {QStringLiteral("FlatButton"), QStringLiteral("TriState"), QStringLiteral("Field"),
             QStringLiteral("Check"), QStringLiteral("Badge")})
        qmlRegisterType(
            QUrl::fromLocalFile(qml + "components/" + control + ".qml"), "OmaChat", 1, 0, control.toUtf8().constData());
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
