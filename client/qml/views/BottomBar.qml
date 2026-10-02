import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// Full-width status bar: identity, live voice session, mic/deafen/settings.
Rectangle {
    id: bar
    signal openSettings()
    readonly property bool compact: width < Metrics.px(900)

    implicitHeight: Metrics.px(52)
    color: Theme.surfaceAlt

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Metrics.px(10)
        anchors.rightMargin: Metrics.px(10)
        spacing: Metrics.px(12)

        // Identity + presence
        Rectangle {
            Layout.preferredWidth: bar.compact ? Metrics.px(46) : Metrics.px(220)
            Layout.fillHeight: true
            Layout.topMargin: Metrics.px(6)
            Layout.bottomMargin: Metrics.px(6)
            radius: Metrics.px(6)
            color: idArea.containsMouse ? Theme.raised : "transparent"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Metrics.px(6)
                spacing: Metrics.px(8)
                Avatar {
                    userId: App.selfId
                    name: App.selfName
                    avatarUrl: App.selfAvatarUrl
                    status: App.selfStatus
                    speaking: App.transmitting
                    size: Metrics.px(32)
                }
                ColumnLayout {
                    visible: !bar.compact
                    spacing: 0
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: App.selfName
                        color: Theme.text
                        font.pixelSize: Metrics.px(13)
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: App.selfStatus === "dnd" ? qsTr("Do not disturb")
                            : App.selfStatus === "idle" ? qsTr("Idle")
                            : App.selfStatus === "online" ? qsTr("Online") : qsTr("Offline")
                        color: Theme.textMuted
                        font.pixelSize: Metrics.px(11)
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
            Layout.topMargin: Metrics.px(6)
            Layout.bottomMargin: Metrics.px(6)
            Layout.preferredWidth: bar.compact ? Math.min(Metrics.px(300), bar.width * 0.5) : Metrics.px(300)
            radius: Metrics.px(6)
            color: Theme.surface
            border.color: Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Metrics.px(10)
                anchors.rightMargin: Metrics.px(4)
                spacing: Metrics.px(8)
                Rectangle {
                    width: Metrics.px(8)
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
                        font.pixelSize: Metrics.px(12)
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: App.voiceChannelName + (App.voiceServerName.length > 0 ? " / " + App.voiceServerName : "")
                              + (App.inputMode === "ptt" ? (App.pttActive ? qsTr(" · transmitting") : qsTr(" · push to talk")) : "")
                        color: Theme.textMuted
                        font.pixelSize: Metrics.px(11)
                        elide: Text.ElideRight
                    }
                }
                Row {
                    visible: App.voiceJoined && App.capabilities.indexOf("video.h264") >= 0
                    spacing: 0
                    IconButton {
                        iconName: "monitor"
                        checkable: true
                        checked: App.sharingScreen
                        enabled: App.sharingScreen || (App.canShareScreen && !App.shareStarting)
                        tip: App.sharingScreen ? qsTr("Stop sharing your screen")
                           : App.shareStarting ? qsTr("Pick a screen or window…")
                           : App.canShareScreen ? qsTr("Share your screen") : qsTr("You cannot share your screen here")
                        onClicked: App.toggleScreenShare()
                    }
                    IconButton {
                        visible: !App.sharingScreen
                        implicitWidth: Metrics.px(18)
                        iconName: "chevron-down"
                        iconSize: Metrics.px(10)
                        tip: qsTr("Screen share options")
                        enabled: !App.shareStarting
                        onClicked: shareOptionsMenu.popup()
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

    MenuPopup {
        id: shareOptionsMenu
        MenuAction {
            text: qsTr("Offer all-application sound when sharing")
            checkable: true
            checked: App.videoSettings.audio === true
            onTriggered: App.setVideo("audio", !checked)
        }
    }
}
