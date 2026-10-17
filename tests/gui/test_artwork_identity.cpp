#include "Harness.hpp"
#include "controllers/AppController.hpp"
#include "platform/ThemeProvider.hpp"
#include "views/AudioLevels.hpp"
#include "views/VideoFrameItem.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>
#include <memory>

namespace {
using namespace omachat;
using omachat::test::waitFor;
QQuickItem* namedItem(QQuickItem* root, const QString& name)
{
    if (root->objectName() == name)
        return root;
    for (auto* child : root->childItems())
        if (auto* found = namedItem(child, name))
            return found;
    return nullptr;
}
QQuickItem* iconAction(QQuickItem* root, const QString& icon)
{
    if (root->property("iconName").toString() == icon)
        return root;
    for (auto* child : root->childItems())
        if (auto* found = iconAction(child, icon))
            return found;
    return nullptr;
}
void expectWithinWindow(QQuickItem* item, QQuickWindow* window)
{
    ASSERT_TRUE(item);
    EXPECT_TRUE(item->isVisible());
    const QRectF bounds = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
    EXPECT_GT(bounds.width(), 0);
    EXPECT_GT(bounds.height(), 0);
    EXPECT_GE(bounds.left(), 0);
    EXPECT_GE(bounds.top(), 0);
    EXPECT_LE(bounds.right(), window->width());
    EXPECT_LE(bounds.bottom(), window->height());
}
class ArtworkIdentity : public ::testing::TestWithParam<bool> {
protected:
    client::ThemeProvider theme{nullptr};
    QTemporaryDir files;
    QQmlEngine engine;
    std::unique_ptr<QObject> mock;
    std::unique_ptr<QObject> windowObject;
    void SetUp() override
    {
        theme.setScale(1.5);
        theme.setReducedMotion(true);
        if (GetParam()) {
            QFile colors(files.filePath("light.toml"));
            ASSERT_TRUE(colors.open(QIODevice::WriteOnly));
            colors.write("background = '#fafafa'\nforeground = '#262626'\naccent = '#2563eb'\n");
            colors.close();
            ASSERT_TRUE(theme.loadFile(colors.fileName()));
        }
        QQmlComponent component(&engine);
        component.setData(R"QML(
import QtQuick
QtObject {
    property ListModel switcher: ListModel {}
    property var previews: ({})
    property var calls: []
    property int callCount: calls.length
    property string lastCall: calls.length ? calls[calls.length - 1] : ""
    property bool requestedReplacement: calls.indexOf("replacement") >= 0
    property var icons: ({})
    property int activated: -1
    property bool voiceJoined: false
    property bool voicePending: false
    property string voiceChannelId: "room"
    property string voiceChannelName: "<b>Voice room</b> with a long name"
    property string voiceServerName: "Long server name"
    property bool voiceConnected: true
    property bool canShareScreen: false
    property bool shareStarting: false
    property bool sharingScreen: false
    property bool muted: false
    property bool deafened: false
    property bool transmitting: false
    property bool pttActive: false
    property string inputMode: "vad"
    property string selfId: "self"
    property string selfName: "Tester"
    property string selfAvatarUrl: ""
    property string selfStatus: "online"
    property var capabilities: []
    property var shortcuts: ({})
    property var videoSettings: ({})
    signal channelDataChanged(string id)
    signal artworkGenerationChanged()
    signal voiceChanged()
    function channelDetails(id) { return {icon_attachment_id: icons[id] || ""} }
    function requestPreview(id, filename, size) { calls = calls.concat([id]) }
    function switcherQuery(text) {}
    function switcherActivate(index) { activated = index }
    function addRows() {
        for (let i = 0; i < 12; ++i) {
            const id = "channel" + i
            switcher.append({itemId:id, kind:i === 0 ? "voice" : "text",
                label:"<b>Long channel name</b> " + i, detail:"Long server name for layout"})
            icons[id] = "icon" + i
        }
    }
    function replaceIcon(id, icon) { icons[id] = icon; channelDataChanged(id) }
    function setPreview(id, url) { const p = Object.assign({}, previews); p[id] = url; previews = p }
})QML",
            QUrl());
        mock.reset(component.create());
        ASSERT_TRUE(mock) << component.errorString().toStdString();
        engine.rootContext()->setContextProperty("mockApp", mock.get());
        ASSERT_TRUE(QMetaObject::invokeMethod(mock.get(), "addRows"));
    }
    void load(const QString& source, const QString& body)
    {
        QFile file(QStringLiteral(OMACHAT_SOURCE_ROOT) + "/client/qml/" + source);
        ASSERT_TRUE(file.open(QIODevice::ReadOnly));
        const QString type = QFileInfo(source).baseName();
        QFile copy(files.filePath(type + ".qml"));
        ASSERT_TRUE(copy.open(QIODevice::WriteOnly));
        copy.write(file.readAll().replace("App.", "mockApp.").replace("target: App", "target: mockApp"));
        copy.close();
        QQmlComponent component(&engine);
        component.setData(
            ("import QtQuick\nimport QtQuick.Controls\nimport OmaChat\nimport \""
                + QUrl::fromLocalFile(files.path()).toString()
                + "\" as Fixture\nApplicationWindow { width:720; height:460; visible:true; " + body + " }")
                .toUtf8(),
            QUrl::fromLocalFile(files.filePath("FixtureWindow.qml")));
        windowObject.reset(component.create());
        ASSERT_TRUE(windowObject) << component.errorString().toStdString();
        auto* window = qobject_cast<QQuickWindow*>(windowObject.get());
        ASSERT_TRUE(window);
        if (qEnvironmentVariableIsSet("OMACHAT_TEST_WAYLAND")) {
            ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
        }
        QTest::qWait(150); // Allow native mapping before compositor dispatch.
        if (qEnvironmentVariableIsSet("OMACHAT_TEST_WAYLAND")) {
            const QString match = QString("pid:%1").arg(QCoreApplication::applicationPid());
            ASSERT_EQ(QProcess::execute("hyprctl",
                          {"eval", QString("hl.dispatch(hl.dsp.window.float({action='set',window='%1'}))").arg(match)}),
                0);
            ASSERT_EQ(QProcess::execute("hyprctl",
                          {"eval",
                              QString("hl.dispatch(hl.dsp.window.resize({x=720,y=460,relative=false,window='%1'}))")
                                  .arg(match)}),
                0);
        }
        ASSERT_TRUE(waitFor([&] { return window->width() == 720; })) << "Actual width: " << window->width();
        ASSERT_TRUE(waitFor([&] { return window->height() == 460; })) << "Actual height: " << window->height();
        QTest::qWait(50);
    }
};

TEST_P(ArtworkIdentity, SwitcherLoadsOnlyVisibleRowsAndRetainsKeyboardFallback)
{
    load("dialogs/QuickSwitcher.qml", "Fixture.QuickSwitcher { objectName: 'switcher' }");
    ASSERT_TRUE(windowObject);
    auto* dialog = windowObject->findChild<QObject*>("switcher");
    ASSERT_TRUE(dialog);
    EXPECT_TRUE(mock->property("callCount").toInt() == 0);
    ASSERT_TRUE(QMetaObject::invokeMethod(dialog, "open"));
    QTest::qWait(80);
    auto* window = qobject_cast<QQuickWindow*>(windowObject.get());
    auto* list = namedItem(window->contentItem(), "quickSwitcherResults");
    ASSERT_TRUE(list);
    EXPECT_LE(list->mapToScene(QPointF(0, list->height())).y(), 460);
    EXPECT_GT(mock->property("callCount").toInt(), 0);
    EXPECT_LT(mock->property("callCount").toInt(), 12);
    auto* row = namedItem(window->contentItem(), "quickSwitcherRow_channel0");
    ASSERT_TRUE(row);
    auto* image = namedItem(row, "quickSwitcherChannelIcon");
    auto* fallback = namedItem(row, "quickSwitcherIconFallback");
    ASSERT_TRUE(image);
    ASSERT_TRUE(fallback);
    EXPECT_TRUE(fallback->isVisible());
    QImage png(20, 20, QImage::Format_RGB32);
    png.fill(Qt::red);
    const QString path = files.filePath("icon.png");
    ASSERT_TRUE(png.save(path));
    ASSERT_TRUE(QMetaObject::invokeMethod(
        mock.get(), "setPreview", Q_ARG(QVariant, "icon0"), Q_ARG(QVariant, QUrl::fromLocalFile(path).toString())));
    ASSERT_TRUE(waitFor([&] { return image->isVisible(); }))
        << "Image source: " << image->property("source").toUrl().toString().toStdString()
        << ", status: " << image->property("status").toInt();
    EXPECT_FALSE(fallback->isVisible());
    EXPECT_TRUE(window->grabWindow().save(QString("/tmp/omachat-switcher-%1.png").arg(GetParam() ? "light" : "dark")));
    ASSERT_TRUE(QMetaObject::invokeMethod(
        mock.get(), "replaceIcon", Q_ARG(QVariant, "channel0"), Q_ARG(QVariant, "replacement")));
    ASSERT_TRUE(waitFor([&] { return fallback->isVisible(); }));
    ASSERT_TRUE(waitFor([&] { return mock->property("requestedReplacement").toBool(); }));
    QTest::keyClick(window, Qt::Key_Down);
    QTest::keyClick(window, Qt::Key_Return);
    ASSERT_TRUE(waitFor([&] { return mock->property("activated").toInt() == 1; }))
        << "Activated index: " << mock->property("activated").toInt();
}

TEST_P(ArtworkIdentity, JoinedVoiceUsesArtworkAndClearsItOnLeave)
{
    mock->setProperty("voiceJoined", true);
    ASSERT_TRUE(
        QMetaObject::invokeMethod(mock.get(), "replaceIcon", Q_ARG(QVariant, "room"), Q_ARG(QVariant, "room-icon")));
    load("views/BottomBar.qml",
        "Fixture.BottomBar { objectName:'bar'; anchors.left:parent.left; anchors.right:parent.right; "
        "anchors.bottom:parent.bottom }");
    ASSERT_TRUE(windowObject);
    auto* window = qobject_cast<QQuickWindow*>(windowObject.get());
    auto* bar = windowObject->findChild<QObject*>("bar");
    ASSERT_TRUE(bar);
    auto* image = namedItem(window->contentItem(), "voiceChannelIcon");
    auto* fallback = namedItem(window->contentItem(), "voiceChannelIconFallback");
    auto* identity = namedItem(window->contentItem(), "voiceSessionIdentity");
    auto* leave = iconAction(window->contentItem(), "hangup");
    ASSERT_TRUE(image);
    ASSERT_TRUE(fallback);
    EXPECT_TRUE(fallback->isVisible());
    expectWithinWindow(identity, window);
    expectWithinWindow(fallback, window);
    expectWithinWindow(leave, window);
    EXPECT_EQ(mock->property("callCount").toInt(), 1);
    ASSERT_TRUE(QMetaObject::invokeMethod(mock.get(), "voiceChanged"));
    EXPECT_EQ(mock->property("callCount").toInt(), 1);
    QImage png(20, 20, QImage::Format_RGB32);
    png.fill(Qt::blue);
    const QString path = files.filePath("voice.png");
    ASSERT_TRUE(png.save(path));
    ASSERT_TRUE(QMetaObject::invokeMethod(
        mock.get(), "setPreview", Q_ARG(QVariant, "room-icon"), Q_ARG(QVariant, QUrl::fromLocalFile(path).toString())));
    ASSERT_TRUE(waitFor([&] { return image->isVisible(); }))
        << "Image source: " << image->property("source").toUrl().toString().toStdString()
        << ", status: " << image->property("status").toInt();
    EXPECT_FALSE(fallback->isVisible());
    expectWithinWindow(image, window);
    expectWithinWindow(identity, window);
    expectWithinWindow(leave, window);
    EXPECT_TRUE(
        window->grabWindow().save(QString("/tmp/omachat-voice-identity-%1.png").arg(GetParam() ? "light" : "dark")));
    mock->setProperty("voiceJoined", false);
    ASSERT_TRUE(QMetaObject::invokeMethod(mock.get(), "voiceChanged"));
    ASSERT_TRUE(waitFor([&] { return bar->property("voiceIconId").toString().isEmpty(); }))
        << "Retained voice icon: " << bar->property("voiceIconId").toString().toStdString();
    ASSERT_TRUE(waitFor([&] { return image->property("source").toUrl().isEmpty(); }))
        << "Retained source after leave: " << image->property("source").toUrl().toString().toStdString()
        << ", status: " << image->property("status").toInt();
    EXPECT_FALSE(identity->isVisible());
}
INSTANTIATE_TEST_SUITE_P(DarkAndLight, ArtworkIdentity, ::testing::Bool());
} // namespace

int main(int argc, char** argv)
{
    if (!qEnvironmentVariableIsSet("OMACHAT_TEST_WAYLAND"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("OMACHAT_THEME", "builtin");
    QGuiApplication app(argc, argv);
    qmlRegisterSingletonType<client::AppController>("OmaChat", 1, 0, "App", &client::AppController::create);
    qmlRegisterSingletonType<client::ThemeProvider>("OmaChat", 1, 0, "Theme", &client::ThemeProvider::create);
    const QString root = QStringLiteral(OMACHAT_SOURCE_ROOT) + "/client/qml/";
    // Runtime URL registration lacks the generated module's implicit imports.
    // Use the actual Metrics source with its module import made explicit.
    QTemporaryDir qmlDir;
    QFile metricsSource(root + "Metrics.qml");
    if (!metricsSource.open(QIODevice::ReadOnly))
        return 1;
    QFile metricsCopy(qmlDir.filePath("Metrics.qml"));
    if (!metricsCopy.open(QIODevice::WriteOnly))
        return 1;
    metricsCopy.write(metricsSource.readAll().replace("import QtQml", "import QtQml\nimport OmaChat"));
    metricsCopy.close();
    qmlRegisterSingletonType(QUrl::fromLocalFile(metricsCopy.fileName()), "OmaChat", 1, 0, "Metrics");
    qmlRegisterType<client::VideoFrameItem>("OmaChat", 1, 0, "VideoFrameItem");
    qmlRegisterType<client::AudioLevels>("OmaChat", 1, 0, "AudioLevels");
    for (const QString directory : {"components", "views", "dialogs", "pages"}) {
        const QDir dir(root + directory);
        for (const auto& name : dir.entryList({"*.qml"}, QDir::Files)) {
            const auto type = name.chopped(4).toUtf8();
            qmlRegisterType(QUrl::fromLocalFile(dir.filePath(name)), "OmaChat", 1, 0, type.constData());
        }
    }
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
