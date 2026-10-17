#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <gtest/gtest.h>
#include <memory>

namespace {
// Load the production gesture functions verbatim. The lightweight model/view
// fixture isolates target resolution and lifecycle from daemon timing; the
// operations suite additionally exercises the production pointer delegates.
class ChannelEdgeScroll : public ::testing::Test {
protected:
    QQmlEngine engine;
    std::unique_ptr<QObject> object;
    QObject* sidebar = nullptr;
    QObject* app = nullptr;
    void SetUp() override
    {
        QFile source(QStringLiteral(OMACHAT_SOURCE_ROOT "/client/qml/views/ChannelSidebar.qml"));
        ASSERT_TRUE(source.open(QIODevice::ReadOnly));
        QByteArray gesture = source.readAll();
        const auto handlerStart = gesture.indexOf("        function onAdministrationFinished(");
        const auto handlerEnd = gesture.indexOf("        function onServerDataChanged(", handlerStart);
        ASSERT_GT(handlerStart, 0);
        ASSERT_GT(handlerEnd, handlerStart);
        const QByteArray finishedHandler = gesture.mid(handlerStart, handlerEnd - handlerStart);
        const auto end = gesture.indexOf("    function focusList() {");
        ASSERT_GT(end, 0);
        gesture.truncate(end);
        gesture = gesture.mid(gesture.indexOf("Rectangle {"));
        gesture.replace("id: sidebar", "id: sidebar; objectName: 'sidebar'");
        gesture.replace("Theme.surface", "'#202020'");
        gesture.replace("App.", "mockApp.");
        gesture.replace("Metrics.px", "metrics.px");
        const QByteArray fixture = R"QML(
import QtQuick
import QtQuick.Controls
Window {
    width: 300; height: 340; visible: true
    QtObject { id: metrics; function px(n) { return n } }
    QtObject {
        id: mockApp; objectName: "mockApp"
        property bool ready: true
        property bool homeSelected: false
        property bool canManageChannels: true
        property var capabilities: ["channel.placement.v1"]
        property string selectedServerId: "server"
        property string accountId: "account"
        property int artworkGeneration: 1
        property var calls: []
        property int rowCount: rows.count
        signal administrationFinished(string operation, string id, string error, var saved)
        function channelDetails(id) { return {parent_id: (id === "nested" || id === "last") ? "cat" : "0"} }
        function channelPlacementSiblings(id, parent) {
            let result = []
            let category = id === "cat" || id === "cat2"
            for (let i = 0; i < rows.count; ++i) {
                let r = rows.get(i)
                if (r.itemId !== id && (r.rowType === "category") === category
                    && r.rowType !== "participant" && (channelDetails(r.itemId).parent_id === parent))
                    result.push({id:r.itemId, name:r.name})
            }
            return result
        }
        function placeChannel(id, parent, before) { calls = calls.concat([{id:id, parent:parent, before:before}]) }
    }
    ListModel {
        id: rows
        Component.onCompleted: {
            append({itemId: "top", rowType: "text", name: "Top"})
            append({itemId: "cat", rowType: "category", name: "First"})
            for (let i = 2; i < 50; ++i)
                append({itemId: "row" + i, rowType: "text", name: "Row " + i})
            append({itemId: "cat2", rowType: "category", name: "Second"})
            append({itemId: "nested", rowType: "voice", name: "Nested"})
            for (let i = 52; i < 79; ++i)
                append({itemId: "row" + i, rowType: "text", name: "Row " + i})
            append({itemId: "last", rowType: "text", name: "Last"})
        }
    }
)QML" + gesture
            + R"QML(
        width: 300; height: 340
        ListView {
            id: list; objectName: "edgeList"; anchors.fill: parent; model: rows
            highlightFollowsCurrentItem: false; boundsBehavior: Flickable.StopAtBounds
            delegate: Rectangle { width: 300; height: 30 }
        }
        Connections { target: mockApp
)QML" + finishedHandler
            + R"QML(
        }
    }
}
)QML";
        QQmlComponent component(&engine);
        component.setData(fixture, QUrl());
        object.reset(component.create());
        ASSERT_TRUE(object) << component.errorString().toStdString();
        sidebar = object->findChild<QObject*>("sidebar");
        app = object->findChild<QObject*>("mockApp");
        ASSERT_TRUE(sidebar);
        ASSERT_TRUE(app);
        QTest::qWait(30);
    }
    void begin(const QString& id = "top", const QString& type = "text")
    {
        ASSERT_TRUE(QMetaObject::invokeMethod(
            sidebar, "beginPlacement", Q_ARG(QVariant, id), Q_ARG(QVariant, type), Q_ARG(QVariant, QPointF(100, 15))));
    }
    void point(const char* method, double y, double x = 100)
    {
        ASSERT_TRUE(QMetaObject::invokeMethod(sidebar, method, Q_ARG(QVariant, QPointF(x, y))));
    }
    QVariantMap target() { return sidebar->property("dropTarget").toMap(); }
    QVariantList calls() { return app->property("calls").toList(); }
    QObject* list() { return object->findChild<QObject*>("edgeList"); }
    QObject* timer() { return object->findChild<QObject*>("channelPlacementEdgeScroll"); }
    void tick() { ASSERT_TRUE(QMetaObject::invokeMethod(sidebar, "scrollPlacementEdge")); }
    double offset() { return list()->property("contentY").toDouble(); }
    bool scrolling() { return timer()->property("running").toBool(); }
};

TEST_F(ChannelEdgeScroll, StationaryPointerCrossesLongListAndRecomputesDestinations)
{
    begin();
    point("updatePlacement", 320);
    ASSERT_TRUE(scrolling());
    const auto initial = target().value("beforeId").toString();
    for (int i = 0; i < 100; ++i)
        tick();
    EXPECT_GT(offset(), 400);
    EXPECT_NE(target().value("beforeId").toString(), initial);
    EXPECT_TRUE(calls().isEmpty());
    EXPECT_EQ(app->property("rowCount").toInt(), 80);
    // A category header beyond the initial viewport remains a valid end target.
    list()->setProperty("contentY", 1230.0);
    ASSERT_TRUE(QMetaObject::invokeMethod(list(), "forceLayout"));
    point("updatePlacement", 285); // Category 2 at content y=1500.
    EXPECT_EQ(target().value("parentId").toString(), "cat2");
    EXPECT_EQ(target().value("beforeId").toString(), "");
    point("finishPlacement", 285);
    ASSERT_EQ(calls().size(), 1);
    EXPECT_EQ(calls().first().toMap().value("parent").toString(), "cat2");
    EXPECT_FALSE(scrolling());
}

TEST_F(ChannelEdgeScroll, BoundsStopTimerAndExposeTopLevelAndCategoryEnd)
{
    for (const QString& type : {QString("text"), QString("category")}) {
        begin(type == "category" ? "cat" : "top", type);
        point("updatePlacement", 339);
        for (int i = 0; i < 260; ++i)
            tick();
        EXPECT_DOUBLE_EQ(offset(), 2060);
        EXPECT_FALSE(scrolling());
        EXPECT_EQ(target().value("parentId").toString(), "0");
        EXPECT_EQ(target().value("beforeId").toString(), "");
        point("finishPlacement", 339);
        EXPECT_FALSE(scrolling());
        sidebar->setProperty("pendingPlacementId", "");
    }
    ASSERT_EQ(calls().size(), 2);
    begin("last");
    point("updatePlacement", 1);
    for (int i = 0; i < 260; ++i)
        tick();
    EXPECT_DOUBLE_EQ(offset(), 0);
    EXPECT_FALSE(scrolling());
    EXPECT_EQ(target().value("beforeId").toString(), "top");
    EXPECT_EQ(app->property("rowCount").toInt(), 80);
}

TEST_F(ChannelEdgeScroll, CenterOutsideEscapeAndContextChangesStopScrolling)
{
    begin();
    point("updatePlacement", 339);
    tick();
    const double moved = offset();
    point("updatePlacement", 170);
    EXPECT_FALSE(scrolling());
    tick();
    EXPECT_DOUBLE_EQ(offset(), moved);
    point("updatePlacement", 339);
    EXPECT_TRUE(scrolling());
    point("updatePlacement", 341);
    EXPECT_FALSE(scrolling());
    EXPECT_TRUE(target().isEmpty());
    tick();
    EXPECT_DOUBLE_EQ(offset(), moved);
    point("updatePlacement", 339);
    QTest::keyClick(qobject_cast<QQuickWindow*>(object.get()), Qt::Key_Escape);
    EXPECT_FALSE(scrolling());
    tick();
    EXPECT_DOUBLE_EQ(offset(), moved);
    for (const char* scope :
        {"ready", "canManageChannels", "capabilities", "selectedServerId", "accountId", "artworkGeneration"}) {
        const auto original = app->property(scope);
        begin();
        point("updatePlacement", 339);
        ASSERT_TRUE(scrolling());
        const QVariant changed = QByteArray(scope) == "artworkGeneration"                ? QVariant(2)
            : QByteArray(scope) == "capabilities"                                        ? QVariant(QStringList{})
            : (QByteArray(scope) == "ready" || QByteArray(scope) == "canManageChannels") ? QVariant(false)
                                                                                         : QVariant("changed");
        app->setProperty(scope, changed);
        tick();
        EXPECT_FALSE(scrolling());
        EXPECT_TRUE(sidebar->property("dragId").toString().isEmpty());
        EXPECT_TRUE(target().isEmpty());
        point("finishPlacement", 339);
        EXPECT_TRUE(calls().isEmpty());
        app->setProperty(scope, original);
    }
}

TEST_F(ChannelEdgeScroll, BoundedStepsRestoreIndexAndReleaseRejectionPreserveOrder)
{
    list()->setProperty("currentIndex", 4);
    begin();
    EXPECT_EQ(list()->property("currentIndex").toInt(), 0);
    point("updatePlacement", 339);
    tick();
    EXPECT_GT(offset(), 0);
    EXPECT_LE(offset(), 12);
    point("finishPlacement", 339);
    EXPECT_EQ(list()->property("currentIndex").toInt(), 4);
    EXPECT_FALSE(scrolling());
    ASSERT_EQ(calls().size(), 1);
    begin(); // Pending requests prohibit another gesture/timer.
    EXPECT_TRUE(sidebar->property("dragId").toString().isEmpty());
    ASSERT_TRUE(QMetaObject::invokeMethod(app, "administrationFinished", Q_ARG(QString, "channel.move"),
        Q_ARG(QString, "top"), Q_ARG(QString, "Destination changed."), Q_ARG(QVariant, QVariantMap{})));
    EXPECT_TRUE(sidebar->property("placementFailed").toBool());
    EXPECT_EQ(app->property("rowCount").toInt(), 80);
    begin();
    point("updatePlacement", 339);
    ASSERT_TRUE(scrolling());
    ASSERT_TRUE(QMetaObject::invokeMethod(sidebar, "cancelPlacementGesture"));
    EXPECT_FALSE(scrolling());
    EXPECT_EQ(list()->property("currentIndex").toInt(), 4);
}
} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
