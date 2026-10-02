#include "platform/ThemeProvider.hpp"

#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTemporaryDir>

#include <gtest/gtest.h>
#include <cmath>
#include <memory>
#include <type_traits>

using omachat::client::ThemeProvider;

TEST(Theme, QmlUsesTheConfiguredInstanceAndTracksChanges)
{
    static_assert(!std::is_default_constructible_v<ThemeProvider>);
    ThemeProvider theme(nullptr);
    theme.setScale(1.5);
    theme.setReducedMotion(true);
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQml
import OmaChatThemeTest
QtObject {
    property real scale: Theme.scale
    property bool reducedMotion: Theme.reducedMotion
    property int animationMs: Theme.animationMs
    property real controlHeight: Metrics.px(32)
}
)", QUrl());
        std::unique_ptr<QObject> object(component.create());
        ASSERT_TRUE(object) << component.errorString().toStdString();
        EXPECT_EQ(ThemeProvider::instance(), &theme);
        EXPECT_DOUBLE_EQ(object->property("scale").toDouble(), 1.5);
        EXPECT_TRUE(object->property("reducedMotion").toBool());
        EXPECT_EQ(object->property("animationMs").toInt(), 0);
        EXPECT_DOUBLE_EQ(object->property("controlHeight").toDouble(), 48.0);
        theme.setScale(1.25);
        theme.setReducedMotion(false);
        EXPECT_DOUBLE_EQ(object->property("scale").toDouble(), 1.25);
        EXPECT_FALSE(object->property("reducedMotion").toBool());
        EXPECT_GT(object->property("animationMs").toInt(), 0);
        EXPECT_DOUBLE_EQ(object->property("controlHeight").toDouble(), 40.0);
    }
    // Engine destruction must not delete the instance owned by main().
    EXPECT_EQ(ThemeProvider::instance(), &theme);
    EXPECT_DOUBLE_EQ(theme.scale(), 1.25);
}

namespace {
double luminance(const QColor& color)
{
    const auto linear = [](double channel) {
        return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
}
double contrast(const QColor& a, const QColor& b)
{
    const auto la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}
void expectReadable(const ThemeProvider& theme)
{
    for (const auto& background : {theme.background(), theme.surface(), theme.surfaceAlt(), theme.raised()})
        EXPECT_GE(contrast(theme.textMuted(), background), 4.5) << background.name().toStdString();
}
}

TEST(Theme, HelperTextIsReadableOnDarkAndLightSurfaces)
{
    ThemeProvider theme(nullptr);
    expectReadable(theme);
    QTemporaryDir dir;
    QFile file(dir.filePath("colors.toml"));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("background = '#fafafa'\nforeground = '#262626'\naccent = '#2563eb'\n");
    file.close();
    ASSERT_TRUE(theme.loadFile(file.fileName()));
    EXPECT_FALSE(theme.dark());
    expectReadable(theme);
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("OMACHAT_THEME", "builtin");
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
