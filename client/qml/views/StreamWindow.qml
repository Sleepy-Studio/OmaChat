import QtQuick
import QtQuick.Window
import QtQuick.Controls
import OmaChat

// A screen share popped out into its own top-level window. Independent of
// the main stream panel: it reads the same shared-memory frame file, so it
// costs nothing extra on the daemon side, and it keeps its own zoom/pan.
Window {
    id: win

    property string streamName: ""
    property string videoPath: ""
    property bool isSelf: false
    property bool pointerActive: false
    property real pointerX: 0.5
    property real pointerY: 0.5
    property double pointerAtMs: 0

    signal closed()

    property double nowMs: Date.now()
    Timer {
        interval: 1000
        repeat: true
        running: !win.isSelf
        onTriggered: win.nowMs = Date.now()
    }

    title: streamName.length > 0 ? qsTr("%1 — Screen share").arg(streamName) : qsTr("Screen share")
    width: 960
    height: 600
    minimumWidth: 320
    minimumHeight: 220
    color: "black"

    onClosing: win.closed()

    Shortcut { sequence: "+"; onActivated: zoomVideo.stepZoom(1.2) }
    Shortcut { sequence: "="; onActivated: zoomVideo.stepZoom(1.2) }
    Shortcut { sequence: "-"; onActivated: zoomVideo.stepZoom(1 / 1.2) }
    Shortcut { sequence: "0"; onActivated: zoomVideo.resetZoom() }

    ZoomableVideo {
        id: zoomVideo
        anchors.fill: parent
        source: win.videoPath
        pointerVisible: win.pointerActive && win.nowMs - win.pointerAtMs < 2000
        pointerNX: win.pointerX
        pointerNY: win.pointerY
    }

    Text {
        anchors.centerIn: parent
        visible: !zoomVideo.hasFrame
        text: win.isSelf ? qsTr("Setting up your preview…") : qsTr("Waiting for %1's screen…").arg(win.streamName)
        color: "#bbbbbb"
        font.pixelSize: 14
    }

    Text {
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.margins: 8
        visible: zoomVideo.hasFrame
        text: zoomVideo.frameSize.width + "×" + zoomVideo.frameSize.height
              + (zoomVideo.zoom > 1.01 ? " · " + Math.round(zoomVideo.zoom * 100) + "%" : "")
        color: "#888888"
        font.pixelSize: 11
    }
}
