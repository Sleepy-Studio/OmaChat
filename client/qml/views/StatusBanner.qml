import QtQuick
import QtQuick.Layouts
import OmaChat

// Connection trouble while a (stale) model is still on screen.
Rectangle {
    id: banner

    readonly property bool active: App.state === "reconnecting" || App.state === "offline"
                                   || App.state === "disconnected" || App.state === "synchronizing"
                                   || App.audioError.length > 0

    visible: active
    implicitHeight: active ? Metrics.px(30) : 0
    color: App.state === "synchronizing" ? Theme.raised : Qt.darker(Theme.warning, 2.6)

    Accessible.role: Accessible.AlertMessage
    Accessible.name: label.text

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Metrics.px(12)
        anchors.rightMargin: Metrics.px(8)
        spacing: Metrics.px(8)

        Icon { name: "alert"; size: Metrics.px(14); color: Theme.warning }
        Text {
            id: label
            Layout.fillWidth: true
            elide: Text.ElideRight
            color: Theme.text
            font.pixelSize: Metrics.px(12)
            text: App.state === "reconnecting" ? qsTr("Connection lost — reconnecting to %1…").arg(App.accountHost)
                : App.state === "offline" ? qsTr("You are offline. OmaChat will reconnect when the network returns.")
                : App.state === "disconnected" ? qsTr("Disconnected from %1").arg(App.accountHost)
                : App.state === "synchronizing" ? qsTr("Synchronizing…")
                : qsTr("Audio: %1").arg(App.audioError)
        }
        FlatButton {
            visible: App.state === "reconnecting" || App.state === "disconnected" || App.state === "offline"
            implicitHeight: Metrics.px(22)
            text: qsTr("Reconnect now")
            onClicked: App.reconnect()
        }
    }
}
