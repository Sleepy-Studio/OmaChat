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
    property string currentUser: App.watchedStreams.length > 0 ? App.watchedStreams[App.watchedStreams.length - 1].userId : ""
    readonly property var current: {
        for (const s of App.watchedStreams)
            if (s.userId === currentUser)
                return s
        return App.watchedStreams.length > 0 ? App.watchedStreams[0] : null
    }

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
                model: App.watchedStreams
                delegate: Rectangle {
                    id: tab
                    required property var modelData
                    readonly property bool active: panel.current && panel.current.userId === modelData.userId
                    implicitHeight: Theme.px(26)
                    implicitWidth: tabRow.implicitWidth + Theme.px(12)
                    radius: Theme.px(4)
                    color: active ? Theme.selection : tabArea.containsMouse ? Theme.raised : "transparent"
                    MouseArea {
                        id: tabArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: panel.currentUser = tab.modelData.userId
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
                            color: Theme.danger
                            Text {
                                id: liveText
                                anchors.centerIn: parent
                                text: qsTr("LIVE")
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

            VideoFrameItem {
                id: video
                anchors.fill: parent
                source: panel.current ? panel.current.path : ""
            }
            Text {
                anchors.centerIn: parent
                visible: !video.hasFrame
                text: qsTr("Waiting for %1's screen…").arg(panel.current ? panel.current.name : "")
                color: "#bbbbbb"
                font.pixelSize: Theme.px(13)
            }
            MouseArea {
                anchors.fill: parent
                onDoubleClicked: panel.toggleExpanded()
            }
            Accessible.role: Accessible.Graphic
            Accessible.name: qsTr("%1's shared screen").arg(panel.current ? panel.current.name : "")
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
    }
}
