import QtQuick
import OmaChat

// Mention counter pill.
Rectangle {
    property int count: 0

    visible: count > 0
    implicitHeight: Theme.px(16)
    implicitWidth: Math.max(implicitHeight, label.implicitWidth + Theme.px(8))
    radius: height / 2
    color: Theme.danger

    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("%n mention(s)", "", count)

    Text {
        id: label
        anchors.centerIn: parent
        text: parent.count > 99 ? "99+" : parent.count
        color: "#ffffff"
        font.pixelSize: Theme.px(10)
        font.bold: true
    }
}
