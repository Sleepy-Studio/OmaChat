import QtQuick
import QtQuick.Controls
import QtMultimedia
import OmaChat

// Inline audio/video attachment player. Media is downloaded on demand.
Item {
    id: root

    property string source: "" // file:// url once downloaded, empty until then
    property string filename: ""
    property bool isVideo: false
    property real maxWidth: Theme.px(400)
    readonly property bool ready: root.source.length > 0
    readonly property bool rotatedVideo: Math.abs(videoOutput.orientation % 180) === 90
    readonly property real videoAspect: videoOutput.sourceRect.height > 0
        ? (rotatedVideo ? videoOutput.sourceRect.height / videoOutput.sourceRect.width
                        : videoOutput.sourceRect.width / videoOutput.sourceRect.height)
        : 16 / 9

    implicitWidth: root.isVideo ? Math.min(Theme.px(400), root.maxWidth, Theme.px(300) * videoAspect)
                                : Math.min(Theme.px(320), root.maxWidth)
    implicitHeight: root.isVideo ? implicitWidth / videoAspect : Theme.px(98)

    // Emitted so the attachment card can trigger the daemon download; the
    // player itself has no network/IPC knowledge.
    signal playRequested()
    signal openRequested()

    onSourceChanged: levels.reset()

    Rectangle {
        anchors.fill: parent
        radius: Theme.px(6)
        color: Theme.surface
        border.color: Theme.border
        clip: true

        MediaPlayer {
            id: player
            source: root.ready ? root.source : ""
            audioOutput: AudioOutput { id: audioOutput }
            audioBufferOutput: root.isVideo ? null : levels.output
            videoOutput: root.isVideo ? videoOutput : null
        }

        AudioLevels { id: levels }

        VideoOutput {
            id: videoOutput
            anchors.fill: parent
            visible: root.isVideo && root.ready
            fillMode: VideoOutput.PreserveAspectFit
        }

        MouseArea {
            anchors.fill: parent
            visible: root.isVideo
            z: 1
            cursorShape: Qt.PointingHandCursor
            onClicked: root.openRequested()
            ToolTip.visible: containsMouse
            ToolTip.delay: 600
            ToolTip.text: qsTr("Open with MPV")
        }

        Text {
            visible: root.isVideo && !root.ready
            anchors.centerIn: parent
            width: parent.width - Theme.px(20)
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideMiddle
            color: Theme.textMuted
            font.pixelSize: Theme.px(12)
            text: root.filename.length > 0 ? root.filename + "\n" + qsTr("Click to open in MPV")
                                           : qsTr("Click to open in MPV")
        }

        Item {
            id: visualizer
            visible: !root.isVideo
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.px(10)
            height: Theme.px(44)

            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                elide: Text.ElideMiddle
                color: Theme.textMuted
                font.pixelSize: Theme.px(11)
                text: root.filename
            }

            Row {
                id: audioBars
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: Theme.px(25)
                spacing: Theme.px(3)

                Repeater {
                    model: 32
                    delegate: Rectangle {
                        required property int index
                        readonly property real amplitude: levels.bars[index] || 0
                        width: Math.max(Theme.px(2), (audioBars.width - 31 * audioBars.spacing) / 32)
                        height: Theme.px(3 + 22 * amplitude)
                        anchors.verticalCenter: parent.verticalCenter
                        radius: width / 2
                        color: Theme.accent
                        opacity: 0.3 + 0.7 * amplitude
                        Behavior on height { NumberAnimation { duration: 100; easing.type: Easing.OutCubic } }
                    }
                }
            }
        }

        // -------------------------------------------------------- controls
        Item {
            id: controls
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: root.isVideo ? Theme.px(8) : 0
            height: root.isVideo ? Theme.px(36) : Theme.px(40)
            z: 2

            Rectangle {
                visible: root.isVideo
                anchors.fill: parent
                radius: Theme.px(4)
                color: Qt.rgba(0, 0, 0, 0.55)
            }

            IconButton {
                id: playButton
                anchors.left: parent.left
                anchors.leftMargin: Theme.px(6)
                anchors.verticalCenter: parent.verticalCenter
                iconName: player.playbackState === MediaPlayer.PlayingState ? "pause" : "play"
                tip: player.playbackState === MediaPlayer.PlayingState ? qsTr("Pause") : qsTr("Play")
                onClicked: {
                    if (!root.ready) {
                        root.playRequested()
                        return
                    }
                    if (player.playbackState === MediaPlayer.PlayingState)
                        player.pause()
                    else
                        player.play()
                }
            }

            Text {
                anchors.left: playButton.right
                anchors.leftMargin: Theme.px(8)
                anchors.verticalCenter: parent.verticalCenter
                visible: !root.ready
                color: Theme.textFaint
                font.pixelSize: Theme.px(12)
                text: qsTr("Tap to load")
            }

            Slider {
                id: seek
                visible: root.ready
                anchors.left: playButton.right
                anchors.leftMargin: Theme.px(8)
                anchors.right: timeLabel.left
                anchors.rightMargin: Theme.px(8)
                anchors.verticalCenter: parent.verticalCenter
                from: 0
                to: Math.max(player.duration, 1)
                value: pressed ? value : player.position
                onMoved: player.setPosition(value)
            }

            Text {
                id: timeLabel
                visible: root.ready
                anchors.right: parent.right
                anchors.rightMargin: Theme.px(8)
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.textFaint
                font.pixelSize: Theme.px(11)
                text: App.formatDuration(player.position) + " / " + App.formatDuration(player.duration)
            }
        }

        Text {
            visible: player.error !== MediaPlayer.NoError && root.ready
            anchors.centerIn: parent
            color: Theme.danger
            font.pixelSize: Theme.px(12)
            text: qsTr("Can't play this file")
        }
    }
}
