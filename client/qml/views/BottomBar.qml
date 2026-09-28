import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// Full-width status bar: identity, live voice session, mic/deafen/settings.
Rectangle {
    id: bar
    signal openSettings()

    implicitHeight: Theme.px(52)
    color: Theme.surfaceAlt

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.px(10)
        anchors.rightMargin: Theme.px(10)
        spacing: Theme.px(12)

        // Identity + presence
        Rectangle {
            Layout.preferredWidth: Theme.px(220)
            Layout.fillHeight: true
            Layout.topMargin: Theme.px(6)
            Layout.bottomMargin: Theme.px(6)
            radius: Theme.px(6)
            color: idArea.containsMouse ? Theme.raised : "transparent"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.px(6)
                spacing: Theme.px(8)
                Avatar {
                    userId: App.selfId
                    name: App.selfName
                    status: App.selfStatus
                    speaking: App.transmitting
                    size: Theme.px(32)
                }
                ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: App.selfName
                        color: Theme.text
                        font.pixelSize: Theme.px(13)
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: App.selfStatus === "dnd" ? qsTr("Do not disturb")
                            : App.selfStatus === "idle" ? qsTr("Idle")
                            : App.selfStatus === "online" ? qsTr("Online") : qsTr("Offline")
                        color: Theme.textMuted
                        font.pixelSize: Theme.px(11)
                        elide: Text.ElideRight
                    }
                }
            }
            MouseArea {
                id: idArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: presenceMenu.popup()
            }
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("%1, status %2. Change status").arg(App.selfName).arg(App.selfStatus)
        }

        // Voice session
        Rectangle {
            visible: App.voiceJoined || App.voicePending
            Layout.fillHeight: true
            Layout.topMargin: Theme.px(6)
            Layout.bottomMargin: Theme.px(6)
            Layout.preferredWidth: Theme.px(300)
            radius: Theme.px(6)
            color: Theme.surface
            border.color: Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.px(10)
                anchors.rightMargin: Theme.px(4)
                spacing: Theme.px(8)
                Rectangle {
                    width: Theme.px(8)
                    height: width
                    radius: width / 2
                    color: App.voiceConnected ? Theme.success : Theme.warning
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text {
                        Layout.fillWidth: true
                        text: App.voicePending ? qsTr("Joining voice…")
                            : App.voiceConnected ? qsTr("Voice connected") : qsTr("Voice connecting…")
                        color: App.voiceConnected ? Theme.success : Theme.warning
                        font.pixelSize: Theme.px(12)
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: App.voiceChannelName + (App.voiceServerName.length > 0 ? " / " + App.voiceServerName : "")
                              + (App.inputMode === "ptt" ? (App.pttActive ? qsTr(" · transmitting") : qsTr(" · push to talk")) : "")
                        color: Theme.textMuted
                        font.pixelSize: Theme.px(11)
                        elide: Text.ElideRight
                    }
                }
                IconButton {
                    iconName: "hangup"
                    danger: true
                    tip: qsTr("Disconnect from voice")
                    onClicked: App.leaveVoice()
                }
            }
            Accessible.role: Accessible.StatusBar
            Accessible.name: qsTr("Voice: %1").arg(App.voiceChannelName)
        }

        Item { Layout.fillWidth: true }

        // Deafen is obvious: both icons turn red and crossed out.
        IconButton {
            iconName: App.muted || App.deafened ? "mic-off" : "mic"
            checkable: true
            checked: App.muted || App.deafened
            tip: (App.muted ? qsTr("Unmute") : qsTr("Mute")) + " (" + (App.shortcuts["toggle_mute"] || "Ctrl+Shift+M") + ")"
            onClicked: App.toggleMute()
        }
        IconButton {
            iconName: App.deafened ? "headphones-off" : "headphones"
            checkable: true
            checked: App.deafened
            tip: (App.deafened ? qsTr("Undeafen") : qsTr("Deafen")) + " (" + (App.shortcuts["toggle_deafen"] || "Ctrl+Shift+D") + ")"
            onClicked: App.toggleDeafen()
        }
        IconButton {
            iconName: "settings"
            tip: qsTr("Settings")
            onClicked: bar.openSettings()
        }
    }

    MenuPopup {
        id: presenceMenu
        MenuAction { text: qsTr("Online"); onTriggered: App.setPresence("online") }
        MenuAction { text: qsTr("Idle"); onTriggered: App.setPresence("idle") }
        MenuAction { text: qsTr("Do not disturb"); onTriggered: App.setPresence("dnd") }
    }
}
