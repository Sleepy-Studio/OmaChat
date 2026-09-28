import QtQuick
import OmaChat

// Presence indicator. Shapes differ as well as colors so the state is
// readable without color vision: filled (online), ring (offline),
// bar (do not disturb), half (idle).
Rectangle {
    id: dot

    property string status: "offline"
    property real size: Theme.px(10)

    width: size
    height: size
    radius: size / 2
    color: status === "online" ? Theme.success
         : status === "dnd" ? Theme.danger
         : status === "idle" ? Theme.idle
         : Theme.surface
    border.width: Math.max(1.5, size * 0.18)
    border.color: status === "offline" ? Theme.textFaint : Theme.surface

    Accessible.role: Accessible.Indicator
    Accessible.name: status === "dnd" ? qsTr("Do not disturb") : status

    Rectangle {
        visible: dot.status === "dnd"
        anchors.centerIn: parent
        width: dot.size * 0.5
        height: Math.max(1.5, dot.size * 0.16)
        color: Theme.surface
    }
    Rectangle {
        visible: dot.status === "idle"
        width: dot.size * 0.42
        height: width
        radius: width / 2
        x: dot.size * 0.12
        y: dot.size * 0.12
        color: Theme.surface
    }
}
