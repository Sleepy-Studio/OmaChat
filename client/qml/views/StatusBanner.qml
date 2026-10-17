import QtQuick
import QtQuick.Layouts
import OmaChat

// Connection trouble while a (stale) model is still on screen.
Rectangle {
    id: banner

    readonly property bool active: App.state === "reconnecting" || App.state === "offline"
                                   || App.state === "disconnected" || App.state === "synchronizing"
                                   || App.audioError.length > 0 || App.failedSendCount > 0

    visible: active
    implicitHeight: active ? Metrics.px(30) : 0
    color: Theme.raised
    border.color: App.state === "synchronizing" ? Theme.border : Theme.warning

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
                : App.audioError.length > 0 ? qsTr("Audio: %1").arg(App.audioError)
                : qsTr("%1 message(s) need attention").arg(App.failedSendCount)
        }
        FlatButton {
            visible: App.failedSendCount > 0
            implicitHeight: Metrics.px(22)
            text: qsTr("Review send")
            onClicked: App.reviewFailedSend()
        }
        FlatButton {
            visible: App.state === "reconnecting" || App.state === "disconnected" || App.state === "offline"
            implicitHeight: Metrics.px(22)
            text: qsTr("Reconnect now")
            onClicked: App.reconnect()
        }
    }
}
