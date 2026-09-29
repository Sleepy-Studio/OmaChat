import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// Screens you are watching, above the chat. One tab per stream; the video
// itself is decoded by omachatd and drawn from shared memory.
Rectangle {
    id: panel

    property bool expanded: false
    signal toggleExpanded()

    color: Theme.background

    // The local user's own share, folded into the same tab strip as a "You"
    // entry so switching between watching someone else and checking your
    // own picture works the same way.
    readonly property var selfEntry: App.sharingScreen
        ? { "userId": "", "name": qsTr("You"), "path": App.selfPreviewPath, "self": true }
        : null
    readonly property var tabs: selfEntry ? App.watchedStreams.concat([selfEntry]) : App.watchedStreams

    property string currentUser: ""
    readonly property var current: {
        for (const s of tabs)
            if (s.self ? currentUser === "__self__" : s.userId === currentUser)
                return s
        return tabs.length > 0 ? tabs[tabs.length - 1] : null
    }
    // A picture-in-picture of your own share while watching someone else's.
    readonly property bool showSelfPip: selfEntry !== null && !(current && current.self)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.px(8)
            Layout.rightMargin: Theme.px(4)
            implicitHeight: Theme.px(36)
            spacing: Theme.px(4)

            Repeater {
                model: panel.tabs
                delegate: Rectangle {
                    id: tab
                    required property var modelData
                    readonly property bool active: panel.current
                        && (modelData.self ? panel.current.self : panel.current.userId === modelData.userId)
                    implicitHeight: Theme.px(26)
                    implicitWidth: tabRow.implicitWidth + Theme.px(12)
                    radius: Theme.px(4)
                    color: active ? Theme.selection : tabArea.containsMouse ? Theme.raised : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.animationMs } }
                    MouseArea {
                        id: tabArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: panel.currentUser = tab.modelData.self ? "__self__" : tab.modelData.userId
                    }
                    Row {
                        id: tabRow
                        anchors.centerIn: parent
                        spacing: Theme.px(6)
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: liveText.implicitWidth + Theme.px(6)
                            height: Theme.px(14)
                            radius: Theme.px(3)
                            color: tab.modelData.self ? Theme.accent : Theme.danger
                            Text {
                                id: liveText
                                anchors.centerIn: parent
                                text: tab.modelData.self ? qsTr("YOU") : qsTr("LIVE")
                                color: Theme.accentText
                                font.pixelSize: Theme.px(9)
                                font.bold: true
                            }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: tab.modelData.name
                            color: Theme.text
                            font.pixelSize: Theme.px(12)
                        }
                        IconButton {
                            visible: !tab.modelData.self
                            anchors.verticalCenter: parent.verticalCenter
                            implicitWidth: Theme.px(18)
                            implicitHeight: Theme.px(18)
                            iconSize: Theme.px(11)
                            iconName: "x"
                            tip: qsTr("Stop watching")
                            onClicked: App.unwatchStream(tab.modelData.userId)
                        }
                    }
                }
            }
            Item { Layout.fillWidth: true }
            IconButton {
                visible: panel.current && !panel.current.self
                iconName: "speaker"
                iconSize: Theme.px(14)
                tip: qsTr("Volume for %1's stream").arg(panel.current ? panel.current.name : "")
                onClicked: streamVolumePopup.popup()
            }
            Text {
                visible: video.hasFrame
                text: video.frameSize.width + "×" + video.frameSize.height
                color: Theme.textFaint
                font.pixelSize: Theme.px(11)
            }
            IconButton {
                iconName: panel.expanded ? "minimize" : "maximize"
                iconSize: Theme.px(15)
                tip: panel.expanded ? qsTr("Show the chat") : qsTr("Hide the chat")
                onClicked: panel.toggleExpanded()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "black"
            clip: true

            VideoFrameItem {
                id: video
                anchors.fill: parent
                source: panel.current ? panel.current.path : ""
            }
            Text {
                anchors.centerIn: parent
                visible: !video.hasFrame
                text: panel.current && panel.current.self
                    ? qsTr("Setting up your preview…")
                    : qsTr("Waiting for %1's screen…").arg(panel.current ? panel.current.name : "")
                color: "#bbbbbb"
                font.pixelSize: Theme.px(13)
            }
            MouseArea {
                anchors.fill: parent
                onDoubleClicked: panel.toggleExpanded()
            }
            Accessible.role: Accessible.Graphic
            Accessible.name: qsTr("%1's shared screen").arg(panel.current ? panel.current.name : "")

            // Picture-in-picture of your own share, so you always know what
            // the other side sees even while watching someone else's screen.
            Rectangle {
                id: pip
                visible: panel.showSelfPip
                width: Theme.px(168)
                height: Theme.px(94)
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: Theme.px(10)
                radius: Theme.px(6)
                color: "black"
                border.color: Theme.border
                border.width: 1
                clip: true
                opacity: pipArea.containsMouse ? 1 : 0.85
                Behavior on opacity { NumberAnimation { duration: Theme.animationMs } }

                VideoFrameItem {
                    id: pipVideo
                    anchors.fill: parent
                    anchors.margins: 1
                    source: panel.selfEntry ? panel.selfEntry.path : ""
                }
                Text {
                    anchors.centerIn: parent
                    visible: !pipVideo.hasFrame
                    text: qsTr("Your screen")
                    color: "#bbbbbb"
                    font.pixelSize: Theme.px(11)
                }
                Rectangle {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.margins: Theme.px(4)
                    width: youLabel.implicitWidth + Theme.px(6)
                    height: Theme.px(14)
                    radius: Theme.px(3)
                    color: Theme.accent
                    Text {
                        id: youLabel
                        anchors.centerIn: parent
                        text: qsTr("YOU")
                        color: Theme.accentText
                        font.pixelSize: Theme.px(9)
                        font.bold: true
                    }
                }
                MouseArea {
                    id: pipArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: panel.currentUser = "__self__"
                }
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
    }

    // Local per-stream volume (never sent to the server).
    Popup {
        id: streamVolumePopup
        property string userId: panel.current ? panel.current.userId : ""
        property string userName: panel.current ? panel.current.name : ""
        function popup() {
            volumeSlider.value = App.userVolume(userId)
            x = (panel.width - width) / 2
            y = Theme.px(40)
            open()
        }
        width: Theme.px(220)
        padding: Theme.px(12)
        background: Rectangle { color: Theme.raised; radius: Theme.px(6); border.color: Theme.border }
        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.px(6)
            Text {
                text: qsTr("Volume for %1").arg(streamVolumePopup.userName)
                color: Theme.text
                font.pixelSize: Theme.px(12)
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            RowLayout {
                Slider {
                    id: volumeSlider
                    Layout.fillWidth: true
                    from: 0
                    to: 200
                    stepSize: 5
                    Accessible.name: qsTr("Stream volume")
                    onMoved: App.setUserVolume(streamVolumePopup.userId, value)
                }
                Text {
                    text: Math.round(volumeSlider.value) + "%"
                    color: Theme.textMuted
                    font.pixelSize: Theme.px(12)
                    Layout.preferredWidth: Theme.px(38)
                }
            }
        }
    }
}
