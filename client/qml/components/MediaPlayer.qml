import QtQuick
import QtQuick.Controls
import QtMultimedia
import OmaChat

// Inline audio/video attachment player. The MPV-style transport element
// (MediaPlayer + AudioOutput/VideoOutput) is only created once the user
// presses play, since attachments aren't downloaded until then.
Item {
    id: root

    property string source: "" // file:// url once downloaded, empty until then
    property bool isVideo: false
    property real maxWidth: Theme.px(400)
    readonly property bool ready: root.source.length > 0

    implicitWidth: Math.min(Theme.px(320), root.maxWidth)
    implicitHeight: root.isVideo ? implicitWidth * 9 / 16 : Theme.px(56)

    // Emitted so the attachment card can trigger the daemon download; the
    // player itself has no network/IPC knowledge.
    signal playRequested()

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
            videoOutput: root.isVideo ? videoOutput : null
        }

        VideoOutput {
            id: videoOutput
            anchors.fill: parent
            visible: root.isVideo && root.ready
            fillMode: VideoOutput.PreserveAspectFit
        }

        // -------------------------------------------------------- controls
        Item {
            id: controls
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: root.isVideo ? Theme.px(8) : 0
            height: root.isVideo ? Theme.px(36) : parent.height
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
