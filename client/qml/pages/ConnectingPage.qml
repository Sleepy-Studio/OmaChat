import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// Connecting / authenticating / synchronizing / first-time reconnecting.
Rectangle {
    id: root
    color: Theme.background

    readonly property string label: App.state === "connecting" ? qsTr("Connecting to %1…").arg(App.accountHost)
                                  : App.state === "authenticating" ? qsTr("Authenticating…")
                                  : App.state === "synchronizing" ? qsTr("Synchronizing…")
                                  : App.state === "reconnecting" ? qsTr("Reconnecting to %1…").arg(App.accountHost)
                                  : App.state === "offline" ? qsTr("You are offline")
                                  : App.state === "disconnected" ? qsTr("Disconnected")
                                  : qsTr("Starting…")

    ColumnLayout {
        anchors.centerIn: parent
        spacing: Theme.px(12)
        width: Math.min(parent.width - Theme.px(48), Theme.px(420))

        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: App.state !== "offline" && App.state !== "disconnected"
            visible: running
        }
        Icon {
            Layout.alignment: Qt.AlignHCenter
            visible: App.state === "offline" || App.state === "disconnected"
            name: "alert"
            size: Theme.px(36)
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: root.label
            color: Theme.text
            font.pixelSize: Theme.px(16)
            font.bold: true
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            visible: App.errorMessage.length > 0
            text: App.errorMessage
            color: Theme.textMuted
            font.pixelSize: Theme.px(12)
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Theme.px(8)
            FlatButton {
                visible: App.state === "disconnected" || App.state === "reconnecting" || App.state === "offline"
                primary: true
                text: qsTr("Reconnect now")
                onClicked: App.reconnect()
            }
            FlatButton {
                visible: App.state === "disconnected"
                text: qsTr("Switch account")
                onClicked: App.logout()
            }
        }
    }
}
