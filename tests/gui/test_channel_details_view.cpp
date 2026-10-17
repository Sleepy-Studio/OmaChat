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
QQuickItem* named(QQuickItem* root, const QString& name)
{
    if (root->objectName() == name)
        return root;
    for (auto* child : root->childItems())
        if (auto* item = named(child, name))
            return item;
    return nullptr;
}
class ChannelDetailsView : public ::testing::TestWithParam<bool> {
protected:
    client::ThemeProvider theme{nullptr};
    QTemporaryDir files;
    QQmlEngine engine;
    std::unique_ptr<QObject> mock, windowObject;
    QObject* dialog = nullptr;
    QQuickWindow* window = nullptr;
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
    property ListModel channels: ListModel {}
    property bool ready: true
    property string state: "connected"
    property string accountId: "account"
    property string selectedServerId: "server"
    property bool canManageChannels: true
    property var capabilities: ["channel.identity.v1"]
    property var previews: ({})
    property var previewErrors: ({})
    property var calls: []
    property int callCount: calls.length
    property var record: ({id:"room",server_id:"server",type:"voice",name:"<b>Room</b>",
        topic:"<b>Topic</b> ".repeat(160), description:"Description ".repeat(300),
        icon_attachment_id:"icon",banner_attachment_id:"banner"})
    signal statusChanged()
    signal selectionChanged()
    signal channelDataChanged(string id)
    signal artworkGenerationChanged()
    function channelDetails(id) { return record }
    function requestPreview(id, filename, size) { calls = calls.concat([id]); const e=Object.assign({},previewErrors); delete e[id]; previewErrors=e }
    function renderChannelDescription(value) { return value }
    function openLink(link) {}
    function preview(id,url) { const p=Object.assign({},previews); p[id]=url; previews=p }
    function fail(id) { const p=Object.assign({},previewErrors); p[id]=true; previewErrors=p }
    function replace() { record=Object.assign({},record,{banner_attachment_id:"replacement",icon_attachment_id:"new-icon"}); channelDataChanged("room") }
    function remove() { record=({}); channels.clear(); channels.append({name:"changed"}); channels.modelReset() }
})QML",
            QUrl());
        mock.reset(component.create());
        ASSERT_TRUE(mock) << component.errorString().toStdString();
        engine.rootContext()->setContextProperty("mockApp", mock.get());
        QFile source(QStringLiteral(OMACHAT_SOURCE_ROOT) + "/client/qml/dialogs/ChannelDetailsDialog.qml");
        ASSERT_TRUE(source.open(QIODevice::ReadOnly));
        QFile copy(files.filePath("ChannelDetailsDialog.qml"));
        ASSERT_TRUE(copy.open(QIODevice::WriteOnly));
        copy.write(source.readAll().replace("App.", "mockApp.").replace("target: App", "target: mockApp"));
        copy.close();
        component.setData(("import QtQuick\nimport QtQuick.Controls\nimport OmaChat\nimport \""
                              + QUrl::fromLocalFile(files.path()).toString()
                              + "\" as Fixture\nApplicationWindow { width:720; height:460; visible:true; "
                                "Fixture.ChannelDetailsDialog {objectName:'details'} }")
                              .toUtf8(),
            QUrl::fromLocalFile(files.filePath("Window.qml")));
        windowObject.reset(component.create());
        ASSERT_TRUE(windowObject) << component.errorString().toStdString();
        window = qobject_cast<QQuickWindow*>(windowObject.get());
        ASSERT_TRUE(window);
        dialog = windowObject->findChild<QObject*>("details");
        ASSERT_TRUE(dialog);
        open();
    }
    bool nativeFloated = false;
    void size(bool compact)
    {
        theme.setScale(compact ? 1.5 : 1.0);
        window->resize(compact ? 720 : 1440, compact ? 460 : 900);
        QTest::qWait(100);
        if (qEnvironmentVariableIsSet("OMACHAT_TEST_WAYLAND")) {
            const QString match = QString("pid:%1").arg(QCoreApplication::applicationPid());
            if (!nativeFloated) {
                ASSERT_EQ(
                    QProcess::execute("hyprctl",
                        {"eval", QString("hl.dispatch(hl.dsp.window.float({action='set',window='%1'}))").arg(match)}),
                    0);
                nativeFloated = true;
            }
            ASSERT_EQ(QProcess::execute("hyprctl",
                          {"eval",
                              QString("hl.dispatch(hl.dsp.window.resize({x=%1,y=%2,relative=false,window='%3'}))")
                                  .arg(compact ? 720 : 1440)
                                  .arg(compact ? 460 : 900)
                                  .arg(match)}),
                0);
            ASSERT_EQ(
                QProcess::execute("hyprctl", {"eval", QString("hl.dispatch(hl.dsp.focus({window='%1'}))").arg(match)}),
                0);
            QTest::qWait(100);
        }
        ASSERT_EQ(window->width(), compact ? 720 : 1440);
        ASSERT_EQ(window->height(), compact ? 460 : 900);
    }
    void open()
    {
        ASSERT_TRUE(QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, "room")));
        QTest::qWait(50);
    }
    QQuickItem* item(const QString& name) { return named(window->contentItem(), name); }
    void bounds(QQuickItem* item)
    {
        ASSERT_TRUE(item);
        EXPECT_TRUE(item->isVisible());
        const auto r = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
        EXPECT_GT(r.width(), 0);
        EXPECT_GT(r.height(), 0);
        EXPECT_GE(r.left(), 0);
        EXPECT_GE(r.top(), 0);
        EXPECT_LE(r.right(), window->width());
        EXPECT_LE(r.bottom(), window->height());
    }
};
TEST_P(ChannelDetailsView, ArtworkLoadingFailureReplacementAndRetryKeepActionsBounded)
{
    size(true);
    auto* banner = item("channelDetailsBanner");
    ASSERT_TRUE(banner);
    auto* fallback = item("channelDetailsIconFallback");
    ASSERT_TRUE(fallback);
    EXPECT_TRUE(fallback->isVisible());
    EXPECT_TRUE(banner->isVisible());
    const qreal height = banner->height();
    EXPECT_GT(height, 0);
    bounds(item("channelDetailsClose"));
    bounds(item("channelDetailsEdit"));
    bounds(item("channelDetailsScroll"));
    EXPECT_EQ(item("channelDetailsName")->property("textFormat").toInt(), 0);
    EXPECT_EQ(item("channelDetailsTopic")->property("textFormat").toInt(), 0);
    const auto* scroll = item("channelDetailsScroll");
    EXPECT_GT(scroll->property("contentHeight").toReal(), scroll->height());
    const QString broken = files.filePath("broken.png");
    QFile corrupt(broken);
    ASSERT_TRUE(corrupt.open(QIODevice::WriteOnly));
    corrupt.write("broken");
    corrupt.close();
    ASSERT_TRUE(QMetaObject::invokeMethod(
        mock.get(), "preview", Q_ARG(QVariant, "banner"), Q_ARG(QVariant, QUrl::fromLocalFile(broken).toString())));
    ASSERT_TRUE(waitFor([&] { return banner->property("imageStatus").toInt() == 3; }));
    ASSERT_TRUE(QMetaObject::invokeMethod(mock.get(), "fail", Q_ARG(QVariant, "banner")));
    EXPECT_EQ(banner->height(), height);
    const int calls = mock->property("callCount").toInt();
    ASSERT_TRUE(QMetaObject::invokeMethod(banner, "retry"));
    EXPECT_EQ(mock->property("callCount").toInt(), calls + 1);
    QImage png(80, 30, QImage::Format_RGB32);
    png.fill(Qt::blue);
    const QString path = files.filePath("valid.png");
    ASSERT_TRUE(png.save(path));
    ASSERT_TRUE(QMetaObject::invokeMethod(
        mock.get(), "preview", Q_ARG(QVariant, "banner"), Q_ARG(QVariant, QUrl::fromLocalFile(path).toString())));
    ASSERT_TRUE(waitFor([&] { return banner->property("imageStatus").toInt() == 1; }));
    EXPECT_EQ(banner->height(), height);
    ASSERT_TRUE(QMetaObject::invokeMethod(mock.get(), "replace"));
    ASSERT_TRUE(waitFor([&] { return banner->property("source").toUrl().isEmpty(); }));
    EXPECT_EQ(banner->height(), height);
    EXPECT_TRUE(fallback->isVisible());
    bounds(item("channelDetailsClose"));
    bounds(item("channelDetailsEdit"));
    auto* viewport = item("channelDetailsScroll")->property("contentItem").value<QQuickItem*>();
    ASSERT_TRUE(viewport);
    viewport->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_End);
    EXPECT_GT(viewport->property("contentY").toReal(), 0);
    EXPECT_TRUE(window->grabWindow().save(
        QString("/tmp/omachat-room-details-%1-compact.png").arg(GetParam() ? "light" : "dark")));
    size(false);
    EXPECT_TRUE(
        window->grabWindow().save(QString("/tmp/omachat-room-details-%1-wide.png").arg(GetParam() ? "light" : "dark")));
    bounds(item("channelDetailsClose"));
    bounds(item("channelDetailsEdit"));
    bounds(banner);
}
TEST_P(ChannelDetailsView, ClearsAccountServerOfflineAndAccessLoss)
{
    mock->setProperty("accountId", "other");
    QMetaObject::invokeMethod(mock.get(), "statusChanged");
    ASSERT_TRUE(waitFor([&] { return !dialog->property("visible").toBool(); }));
    EXPECT_TRUE(dialog->property("details").toMap().isEmpty());
    mock->setProperty("accountId", "account");
    open();
    mock->setProperty("selectedServerId", "foreign");
    QMetaObject::invokeMethod(mock.get(), "selectionChanged");
    ASSERT_TRUE(waitFor([&] { return !dialog->property("visible").toBool(); }));
    open();
    EXPECT_FALSE(dialog->property("visible").toBool());
    EXPECT_TRUE(dialog->property("details").toMap().isEmpty());
    mock->setProperty("selectedServerId", "server");
    open();
    mock->setProperty("ready", false);
    QMetaObject::invokeMethod(mock.get(), "statusChanged");
    ASSERT_TRUE(waitFor([&] { return !dialog->property("visible").toBool(); }));
    EXPECT_TRUE(dialog->property("details").toMap().isEmpty());
    mock->setProperty("ready", true);
    open();
    ASSERT_TRUE(QMetaObject::invokeMethod(mock.get(), "remove"));
    ASSERT_TRUE(waitFor([&] { return !dialog->property("visible").toBool(); }));
    EXPECT_TRUE(dialog->property("details").toMap().isEmpty());
}
TEST_P(ChannelDetailsView, ReopenDuringVisibleOrClosingPopupKeepsNewContext)
{
    theme.setReducedMotion(false);
    const auto original = dialog->property("details").toMap();
    ASSERT_TRUE(QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, "foreign")));
    EXPECT_EQ(dialog->property("details").toMap(), original);
    ASSERT_TRUE(QMetaObject::invokeMethod(dialog, "close"));
    QTest::qWait(250);
    open();
    EXPECT_TRUE(dialog->property("visible").toBool());
    QTest::qWait(250);
    EXPECT_EQ(dialog->property("channelId").toString(), "room");
    EXPECT_EQ(dialog->property("details").toMap(), original);
    mock->setProperty("state", "offline");
    QMetaObject::invokeMethod(mock.get(), "statusChanged");
    EXPECT_TRUE(dialog->property("visible").toBool());
    EXPECT_FALSE(item("channelDetailsEdit")->isVisible());
}
INSTANTIATE_TEST_SUITE_P(DarkAndLight, ChannelDetailsView, ::testing::Bool());
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
