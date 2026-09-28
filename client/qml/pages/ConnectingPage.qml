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
                // A never-lands connection attempt (e.g. a bad host typed
                // for a new account, or a dead server) must never trap you
                // here with no way out.
                visible: App.accounts.length > 1
                text: qsTr("Switch account")
                onClicked: accountSwitchMenu.popup()
            }
        }
    }

    MenuPopup {
        id: accountSwitchMenu
        Instantiator {
            model: App.accounts
            delegate: MenuAction {
                required property var modelData
                text: modelData.username + "@" + modelData.host
                      + (modelData.state !== "connected" ? " — " + modelData.state.replace("_", " ") : "")
                enabled: !modelData.active
                onTriggered: App.switchAccount(modelData.id)
            }
            onObjectAdded: (index, object) => accountSwitchMenu.insertAction(index, object)
            onObjectRemoved: (index, object) => accountSwitchMenu.removeAction(object)
        }
    }
}
