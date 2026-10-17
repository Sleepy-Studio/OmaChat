import QtQuick
import OmaChat

// Mention counter pill.
Rectangle {
    property int count: 0

    visible: count > 0
    implicitHeight: Metrics.px(16)
    implicitWidth: Math.max(implicitHeight, label.implicitWidth + Metrics.px(8))
    radius: height / 2
    color: Theme.danger

    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("%n mention(s)", "", count)

    Text {
        id: label
        anchors.centerIn: parent
        text: parent.count > 99 ? "99+" : parent.count
        color: Theme.dangerText
        font.pixelSize: Metrics.px(10)
        font.bold: true
    }
}
