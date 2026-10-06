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
class ChannelDrag : public ::testing::Test {
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
        signal administrationFinished(string operation, string id, string error, var saved)
        function channelDetails(id) { return {parent_id: id === "nested" ? "cat" : "0"} }
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
        ListElement { itemId: "top"; rowType: "text"; name: "Top" }
        ListElement { itemId: "cat"; rowType: "category"; name: "Category" }
        ListElement { itemId: "nested"; rowType: "voice"; name: "Nested" }
        ListElement { itemId: "participant"; rowType: "participant"; name: "Member" }
        ListElement { itemId: "cat2"; rowType: "category"; name: "Second" }
        ListElement { itemId: "last"; rowType: "text"; name: "Last" }
    }
)QML" + gesture
            + R"QML(
        width: 300; height: 340
        ListView {
            id: list; anchors.fill: parent; model: rows
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
};

TEST_F(ChannelDrag, ThresholdAndInvalidTargetsDoNotSubmit)
{
    begin();
    point("updatePlacement", 19);
    EXPECT_FALSE(sidebar->property("dragging").toBool());
    point("updatePlacement", 110); // Participant.
    EXPECT_TRUE(target().isEmpty());
    point("finishPlacement", 110);
    EXPECT_TRUE(calls().isEmpty());
    begin();
    point("updatePlacement", 45, -1); // Outside list.
    EXPECT_TRUE(target().isEmpty());
    point("finishPlacement", 45, -1);
    EXPECT_TRUE(calls().isEmpty());
}

TEST_F(ChannelDrag, ChannelsMoveIntoCategoryBeforeSiblingAndOutAtEnd)
{
    begin();
    point("updatePlacement", 45);
    EXPECT_EQ(target().value("parentId").toString(), "cat");
    EXPECT_EQ(target().value("beforeId").toString(), "");
    point("updatePlacement", 65);
    EXPECT_EQ(target().value("parentId").toString(), "cat");
    EXPECT_EQ(target().value("beforeId").toString(), "nested");
    point("finishPlacement", 65);
    ASSERT_EQ(calls().size(), 1);
    EXPECT_EQ(calls().first().toMap().value("before").toString(), "nested");
    // Pending requests prohibit a second gesture; there is no local model move.
    begin("nested", "voice");
    EXPECT_TRUE(sidebar->property("dragId").toString().isEmpty());
    sidebar->setProperty("pendingPlacementId", "");
    begin("nested", "voice");
    point("finishPlacement", 250); // A release alone never becomes a drag.
    EXPECT_EQ(calls().size(), 1);
    begin("nested", "voice");
    point("updatePlacement", 250);
    EXPECT_EQ(target().value("parentId").toString(), "0");
    point("finishPlacement", 250);
    ASSERT_EQ(calls().size(), 2);
    EXPECT_EQ(calls().last().toMap().value("parent").toString(), "0");
    EXPECT_EQ(calls().last().toMap().value("before").toString(), "");
}

TEST_F(ChannelDrag, CategoriesOnlyTargetCategorySiblings)
{
    begin("cat", "category");
    point("updatePlacement", 165); // Ordinary channel.
    EXPECT_TRUE(target().isEmpty());
    point("updatePlacement", 125);
    EXPECT_EQ(target().value("beforeId").toString(), "cat2");
    point("updatePlacement", 145);
    EXPECT_EQ(target().value("beforeId").toString(), "");
    point("finishPlacement", 145);
    ASSERT_EQ(calls().size(), 1);
    EXPECT_EQ(calls().first().toMap().value("parent").toString(), "0");
}

TEST_F(ChannelDrag, EscapeCancelsAndRejectedCompletionKeepsAuthoritativeModel)
{
    begin();
    point("updatePlacement", 45);
    auto* window = qobject_cast<QQuickWindow*>(object.get());
    ASSERT_TRUE(window);
    QTest::keyClick(window, Qt::Key_Escape);
    EXPECT_TRUE(sidebar->property("dragId").toString().isEmpty());
    EXPECT_TRUE(target().isEmpty());
    point("finishPlacement", 45);
    EXPECT_TRUE(calls().isEmpty());
    begin();
    point("updatePlacement", 45);
    point("finishPlacement", 45);
    ASSERT_EQ(calls().size(), 1);
    ASSERT_TRUE(QMetaObject::invokeMethod(app, "administrationFinished", Q_ARG(QString, "channel.move"),
        Q_ARG(QString, "top"), Q_ARG(QString, "Destination changed."), Q_ARG(QVariant, QVariantMap{})));
    EXPECT_TRUE(sidebar->property("pendingPlacementId").toString().isEmpty());
    EXPECT_TRUE(sidebar->property("placementFailed").toBool());
    EXPECT_EQ(sidebar->property("placementFeedback").toString(), "Destination changed.");
    begin();
    point("updatePlacement", 65);
    // The model still places Nested inside Category after the rejected request.
    EXPECT_EQ(target().value("parentId").toString(), "cat");
    EXPECT_EQ(target().value("beforeId").toString(), "nested");
}

TEST_F(ChannelDrag, PermissionCapabilityAndContextInvalidateBeforeSubmission)
{
    for (const char* permission : {"ready", "canManageChannels"}) {
        app->setProperty(permission, false);
        begin();
        EXPECT_TRUE(sidebar->property("dragId").toString().isEmpty());
        app->setProperty(permission, true);
    }
    app->setProperty("capabilities", QStringList{});
    begin();
    EXPECT_TRUE(sidebar->property("dragId").toString().isEmpty());
    app->setProperty("capabilities", QStringList{"channel.placement.v1"});
    for (const char* scope : {"selectedServerId", "accountId", "artworkGeneration"}) {
        const QVariant original = app->property(scope);
        begin();
        point("updatePlacement", 45);
        app->setProperty(scope, scope == QByteArray("artworkGeneration") ? QVariant(2) : QVariant("changed"));
        point("finishPlacement", 45);
        EXPECT_TRUE(calls().isEmpty());
        EXPECT_TRUE(target().isEmpty());
        app->setProperty(scope, original);
    }
    begin();
    point("updatePlacement", 45);
    ASSERT_TRUE(QMetaObject::invokeMethod(sidebar, "cancelPlacementGesture"));
    point("finishPlacement", 45);
    EXPECT_TRUE(calls().isEmpty());
}
} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
