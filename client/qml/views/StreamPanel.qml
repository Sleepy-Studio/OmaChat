import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml
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

    // Refreshed once a second so staleness below can react to wall-clock
    // time passing, not just new status pushes from the daemon.
    property double nowMs: Date.now()
    Timer {
        interval: 1000
        repeat: true
        running: panel.current && !panel.current.self
        onTriggered: panel.nowMs = Date.now()
    }
    // "connecting": nothing ever received. "live": recent frames decoding.
    // "stalled": packets are still arriving but decoding has stopped, which
    // in practice is the closest we can tell "video froze, audio may still
    // be fine" apart without a dedicated audio-activity signal from the
    // daemon. "lost": nothing has arrived in a while either.
    readonly property string streamState: {
        const c = current
        if (!c || c.self)
            return "self"
        if (!c.lastPacketMs)
            return "connecting"
        if (nowMs - c.lastPacketMs > 4000)
            return "lost"
        if (c.lastDecodedMs && nowMs - c.lastDecodedMs > 3000)
            return "stalled"
        return "live"
    }

    function keyFor(entry) { return entry.self ? "__self__" : entry.userId }
    function tabForKey(key) {
        for (const t of tabs)
            if (panel.keyFor(t) === key)
                return t
        return null
    }

    // For the global keyboard shortcuts in MainView.qml, which act on
    // whichever stream is currently shown inline.
    function zoomStep(factor) { video.stepZoom(factor) }
    function resetVideoZoom() { video.resetZoom() }
    function popOutCurrent() {
        if (current)
            popOut(keyFor(current))
    }

    // Caps how often pointer moves go over IPC while dragging; a release is
    // always sent immediately so the dot disappears for viewers promptly.
    property double lastPointerSendMs: 0
    function sendPointerThrottled(nx, ny, pressed) {
        if (!pressed) {
            App.sendPointer(false, nx, ny)
            lastPointerSendMs = 0
            return
        }
        const now = Date.now()
        if (now - lastPointerSendMs < 40)
            return
        lastPointerSendMs = now
        App.sendPointer(true, nx, ny)
    }

    // Remembered across pop-outs so each new window opens at the size you
    // last left one, instead of always resetting to the same default.
    property size poppedOutSize: Qt.size(960, 600)

    // Laser pointer: only meaningful while looking at your own share; also
    // covers sharing ending, since that clears the self tab and current
    // moves (or becomes null), tripping this the same way.
    property bool pointerMode: false
    onCurrentChanged: if (!current || !current.self) pointerMode = false

    // userId (or "__self__") -> true for every stream popped into its own window.
    property var poppedOut: ({})
    function popOut(key) {
        const next = Object.assign({}, poppedOut)
        next[key] = true
        poppedOut = next
    }
    function closePopout(key) {
        if (!(key in poppedOut))
            return
        const next = Object.assign({}, poppedOut)
        delete next[key]
        poppedOut = next
    }
    // Drop pop-outs for streams that stopped being watched (or self-share ended).
    onTabsChanged: {
        const live = new Set(tabs.map(panel.keyFor))
        const next = Object.assign({}, poppedOut)
        let changed = false
        for (const k of Object.keys(next)) {
            if (!live.has(k)) {
                delete next[k]
                changed = true
            }
        }
        if (changed)
            poppedOut = next
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
                        onClicked: panel.currentUser = panel.keyFor(tab.modelData)
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
                        Rectangle {
                            visible: !tab.modelData.self && tab.modelData.quality === "poor"
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.px(7)
                            height: width
                            radius: width / 2
                            color: Theme.warning
                            Accessible.ignored: true
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: tab.modelData.name
                            color: Theme.text
                            font.pixelSize: Theme.px(12)
                        }
                        IconButton {
                            visible: !panel.poppedOut[panel.keyFor(tab.modelData)]
                            anchors.verticalCenter: parent.verticalCenter
                            implicitWidth: Theme.px(18)
                            implicitHeight: Theme.px(18)
                            iconSize: Theme.px(11)
                            iconName: "pop-out"
                            tip: qsTr("Pop out into its own window")
                            onClicked: panel.popOut(panel.keyFor(tab.modelData))
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
            Text {
                visible: panel.current && !panel.current.self
                    && (panel.current.quality === "poor" || panel.streamState === "stalled" || panel.streamState === "lost")
                text: panel.streamState === "lost" ? qsTr("Connection lost")
                    : panel.streamState === "stalled" ? qsTr("Picture frozen")
                    : qsTr("Poor connection")
                color: Theme.warning
                font.pixelSize: Theme.px(11)
            }
            IconButton {
                visible: panel.current && !panel.current.self
                iconName: "speaker"
                iconSize: Theme.px(14)
                tip: qsTr("Volume for %1's stream").arg(panel.current ? panel.current.name : "")
                onClicked: streamVolumePopup.popup()
            }
            IconButton {
                visible: panel.current && panel.current.self
                iconName: "pointer"
                iconSize: Theme.px(14)
                checkable: true
                checked: panel.pointerMode
                iconColor: checked ? Theme.accent : Theme.textMuted
                tip: panel.pointerMode ? qsTr("Stop pointing") : qsTr("Point at your screen (viewers see a dot where you click)")
                onClicked: panel.pointerMode = !panel.pointerMode
            }
            Text {
                visible: video.hasFrame
                text: video.frameSize.width + "×" + video.frameSize.height
                      + (video.zoom > 1.01 ? " · " + Math.round(video.zoom * 100) + "%" : "")
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

            ZoomableVideo {
                id: video
                anchors.fill: parent
                source: panel.current ? panel.current.path : ""
                onDoubleClicked: panel.toggleExpanded()

                pointerCaptureEnabled: !!(panel.current && panel.current.self && panel.pointerMode)
                onPointerInput: (nx, ny, pressed) => panel.sendPointerThrottled(nx, ny, pressed)

                pointerVisible: !!(panel.current && !panel.current.self && panel.current.pointerActive
                                    && panel.nowMs - panel.current.pointerAtMs < 2000)
                pointerNX: panel.current ? panel.current.pointerX : 0.5
                pointerNY: panel.current ? panel.current.pointerY : 0.5
            }
            Text {
                anchors.centerIn: parent
                visible: !video.hasFrame || panel.streamState === "lost"
                text: {
                    const c = panel.current
                    if (!c)
                        return ""
                    if (c.self)
                        return qsTr("Setting up your preview…")
                    if (panel.streamState === "lost")
                        return qsTr("Lost the connection to %1's screen…").arg(c.name)
                    return qsTr("Waiting for %1's screen…").arg(c.name)
                }
                color: "#bbbbbb"
                font.pixelSize: Theme.px(13)
            }
            // Video decoding stalled but packets are still arriving, which in
            // practice is the best we can tell apart from a hard drop: the
            // daemon has no separate signal for "screen audio still flowing".
            Rectangle {
                visible: video.hasFrame && panel.streamState === "stalled"
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: Theme.px(10)
                width: stalledText.implicitWidth + Theme.px(16)
                height: Theme.px(24)
                radius: Theme.px(12)
                color: Qt.rgba(0, 0, 0, 0.6)
                Text {
                    id: stalledText
                    anchors.centerIn: parent
                    text: qsTr("Picture frozen — reconnecting…")
                    color: "#eeeeee"
                    font.pixelSize: Theme.px(11)
                }
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

    // One top-level window per popped-out stream. Instantiator (not
    // Repeater) because its delegates are Windows, not Items.
    Instantiator {
        model: Object.keys(panel.poppedOut)
        delegate: StreamWindow {
            id: popout
            required property string modelData
            readonly property var entry: panel.tabForKey(modelData)
            streamName: entry ? entry.name : ""
            videoPath: entry ? entry.path : ""
            isSelf: entry ? !!entry.self : false
            pointerActive: entry ? !!entry.pointerActive : false
            pointerX: entry ? entry.pointerX : 0.5
            pointerY: entry ? entry.pointerY : 0.5
            pointerAtMs: entry ? entry.pointerAtMs : 0
            width: panel.poppedOutSize.width
            height: panel.poppedOutSize.height
            visible: true
            onWidthChanged: panel.poppedOutSize = Qt.size(width, height)
            onHeightChanged: panel.poppedOutSize = Qt.size(width, height)
            onClosed: panel.closePopout(modelData)
        }
    }
}
