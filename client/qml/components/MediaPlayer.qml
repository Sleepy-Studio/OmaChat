import QtQuick
import QtQuick.Controls
import QtMultimedia as Multimedia
import OmaChat

// Inline audio/video attachment player. Media is downloaded on demand.
Item {
    id: root

    property string source: "" // file:// url once downloaded, empty until then
    property string filename: ""
    property string thumbnail: ""
    property bool thumbnailFailed: false
    property bool isVideo: false
    property real maxWidth: Theme.px(400)
    property bool pendingPlay: false
    property alias volume: volumeSlider.value
    property bool muted: false
    readonly property bool ready: root.source.length > 0
    readonly property bool playing: player.playbackState === Multimedia.MediaPlayer.PlayingState
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

    function startPendingPlayback() {
        if (!root.pendingPlay || !root.ready)
            return
        if (player.mediaStatus === Multimedia.MediaPlayer.EndOfMedia)
            player.setPosition(0)
        if (player.mediaStatus === Multimedia.MediaPlayer.LoadedMedia
                || player.mediaStatus === Multimedia.MediaPlayer.BufferedMedia
                || player.mediaStatus === Multimedia.MediaPlayer.BufferingMedia
                || player.mediaStatus === Multimedia.MediaPlayer.EndOfMedia)
            player.play()
    }

    onSourceChanged: {
        levels.reset()
        Qt.callLater(root.startPendingPlayback)
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.px(6)
        color: Theme.surface
        border.color: Theme.border
        clip: true

        Multimedia.MediaPlayer {
            id: player
            source: root.ready ? root.source : ""
            audioOutput: Multimedia.AudioOutput {
                id: audioOutput
                volume: root.volume
                muted: root.muted
            }
            audioBufferOutput: levels.output
            videoOutput: root.isVideo ? videoOutput : null
            onMediaStatusChanged: root.startPendingPlayback()
            onPlaybackStateChanged: {
                if (playbackState === Multimedia.MediaPlayer.PlayingState)
                    root.pendingPlay = false
            }
            onErrorOccurred: root.pendingPlay = false
        }

        AudioLevels { id: levels }

        Image {
            anchors.fill: parent
            visible: root.isVideo && (player.playbackState === Multimedia.MediaPlayer.StoppedState && player.position === 0)
            source: root.isVideo ? root.thumbnail : ""
            asynchronous: true
            fillMode: Image.PreserveAspectCrop
        }

        Multimedia.VideoOutput {
            id: videoOutput
            anchors.fill: parent
            visible: root.isVideo && root.ready
                     && (player.playbackState !== Multimedia.MediaPlayer.StoppedState || player.position > 0)
            fillMode: Multimedia.VideoOutput.PreserveAspectFit
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
            visible: root.isVideo && root.thumbnail.length === 0
            anchors.centerIn: parent
            width: parent.width - Theme.px(20)
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideMiddle
            color: Theme.textMuted
            font.pixelSize: Theme.px(12)
            text: root.thumbnailFailed ? root.filename : qsTr("Loading video preview…")
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

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.openRequested()
                ToolTip.visible: containsMouse
                ToolTip.delay: 600
                ToolTip.text: qsTr("Open in default media player")
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
                iconName: player.playbackState === Multimedia.MediaPlayer.PlayingState ? "pause" : "play"
                tip: player.playbackState === Multimedia.MediaPlayer.PlayingState ? qsTr("Pause") : qsTr("Play")
                onClicked: {
                    if (root.playing) {
                        root.pendingPlay = false
                        player.pause()
                    } else {
                        root.pendingPlay = true
                        if (root.ready)
                            root.startPendingPlayback()
                        else
                            root.playRequested()
                    }
                }
            }

            Text {
                anchors.left: playButton.right
                anchors.leftMargin: Theme.px(8)
                anchors.verticalCenter: parent.verticalCenter
                visible: !root.ready
                color: Theme.textFaint
                font.pixelSize: Theme.px(12)
                text: root.pendingPlay ? qsTr("Loading…") : qsTr("Play in chat")
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

            Item {
                id: volumeControls
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.px(98)
                height: Theme.px(32)

                IconButton {
                    id: muteButton
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.px(28)
                    height: width
                    iconName: "speaker"
                    iconSize: Theme.px(16)
                    iconColor: root.muted || root.volume === 0 ? Theme.textFaint : Theme.textMuted
                    tip: root.muted ? qsTr("Unmute") : qsTr("Mute")
                    onClicked: {
                        if (root.muted && root.volume === 0)
                            root.volume = 0.5
                        root.muted = !root.muted
                    }
                }

                Slider {
                    id: volumeSlider
                    anchors.left: muteButton.right
                    anchors.leftMargin: Theme.px(2)
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    from: 0
                    to: 1
                    stepSize: 0.01
                    value: 1.0
                    Accessible.name: qsTr("Media volume")
                    Accessible.description: qsTr("%1 percent").arg(Math.round(value * 100))
                    onMoved: {
                        root.muted = false
                    }
                    ToolTip.visible: hovered || pressed
                    ToolTip.text: Math.round(value * 100) + "%"
                }
            }

            Text {
                id: timeLabel
                visible: root.ready
                anchors.right: volumeControls.left
                anchors.rightMargin: Theme.px(6)
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.textFaint
                font.pixelSize: Theme.px(11)
                text: App.formatDuration(player.position) + " / " + App.formatDuration(player.duration)
            }
        }

        Text {
            visible: player.error !== Multimedia.MediaPlayer.NoError && root.ready
            anchors.centerIn: parent
            color: Theme.danger
            font.pixelSize: Theme.px(12)
            text: qsTr("Can't play this file")
        }
    }
}
