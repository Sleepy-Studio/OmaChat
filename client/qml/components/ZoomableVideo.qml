import QtQuick
import OmaChat

// A decoded video frame (see VideoFrameItem) with scroll-to-zoom and
// drag-to-pan once zoomed. Used for both the inline stream panel and
// popped-out stream windows so the interaction behaves identically.
Item {
    id: root

    property string source: ""
    readonly property bool hasFrame: video.hasFrame
    readonly property size frameSize: video.frameSize

    readonly property real minZoom: 1.0
    readonly property real maxZoom: 6.0
    property real zoom: 1.0
    property real panX: 0
    property real panY: 0

    // The actual displayed picture within this item's bounds (VideoFrameItem
    // letterboxes to keep the source aspect ratio), in the same untransformed
    // coordinate space zoom/pan operate in. Used to map the pointer overlay.
    readonly property rect contentRect: {
        if (!hasFrame || frameSize.width <= 0 || frameSize.height <= 0 || video.width <= 0 || video.height <= 0)
            return Qt.rect(0, 0, video.width, video.height)
        const srcAspect = frameSize.width / frameSize.height
        const dstAspect = video.width / video.height
        if (srcAspect > dstAspect) {
            const h = video.width / srcAspect
            return Qt.rect(0, (video.height - h) / 2, video.width, h)
        }
        const w = video.height * srcAspect
        return Qt.rect((video.width - w) / 2, 0, w, video.height)
    }

    // Emitted on a double click while not zoomed in, so a parent can use it
    // for its own purpose (e.g. toggling an expanded layout) without it
    // being swallowed by the zoom-reset gesture.
    signal doubleClicked()

    // Sharer side: while true, press-and-drag sends normalized pointer
    // positions instead of panning, so you can point at your own share.
    property bool pointerCaptureEnabled: false
    signal pointerInput(real nx, real ny, bool pressed)

    // Viewer side: renders a dot at the sharer's last reported position.
    property bool pointerVisible: false
    property real pointerNX: 0.5
    property real pointerNY: 0.5

    clip: true

    onSourceChanged: resetZoom()

    function resetZoom() {
        zoom = minZoom
        panX = 0
        panY = 0
    }

    // Keyboard/menu driven zoom (wheel zoom below handles its own centering).
    function stepZoom(factor) {
        zoom = Math.min(maxZoom, Math.max(minZoom, zoom * factor))
        clampPan()
    }

    function clampPan() {
        const maxX = Math.max(0, (root.width * (zoom - 1)) / 2)
        const maxY = Math.max(0, (root.height * (zoom - 1)) / 2)
        panX = Math.max(-maxX, Math.min(maxX, panX))
        panY = Math.max(-maxY, Math.min(maxY, panY))
    }

    VideoFrameItem {
        id: video
        anchors.fill: parent
        source: root.source
        transform: [
            Scale { origin.x: video.width / 2; origin.y: video.height / 2; xScale: root.zoom; yScale: root.zoom },
            Translate { x: root.panX; y: root.panY }
        ]

        Rectangle {
            visible: root.pointerVisible
            width: 16
            height: 16
            radius: 8
            color: "#e5484d"
            border.color: "white"
            border.width: 2
            x: root.contentRect.x + root.pointerNX * root.contentRect.width - width / 2
            y: root.contentRect.y + root.pointerNY * root.contentRect.height - height / 2
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.pointerCaptureEnabled ? Qt.CrossCursor
            : root.zoom > root.minZoom ? (pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor) : Qt.ArrowCursor

        property real lastX
        property real lastY

        function normalized(mouse) {
            const r = root.contentRect
            const nx = r.width > 0 ? (mouse.x - r.x) / r.width : 0.5
            const ny = r.height > 0 ? (mouse.y - r.y) / r.height : 0.5
            return Qt.point(Math.max(0, Math.min(1, nx)), Math.max(0, Math.min(1, ny)))
        }

        onPressed: mouse => {
            lastX = mouse.x
            lastY = mouse.y
            if (root.pointerCaptureEnabled) {
                const n = normalized(mouse)
                root.pointerInput(n.x, n.y, true)
            }
        }
        onPositionChanged: mouse => {
            if (root.pointerCaptureEnabled) {
                if (pressed) {
                    const n = normalized(mouse)
                    root.pointerInput(n.x, n.y, true)
                }
                return
            }
            if (!pressed || root.zoom <= root.minZoom)
                return
            root.panX += mouse.x - lastX
            root.panY += mouse.y - lastY
            lastX = mouse.x
            lastY = mouse.y
            root.clampPan()
        }
        onReleased: mouse => {
            if (root.pointerCaptureEnabled) {
                const n = normalized(mouse)
                root.pointerInput(n.x, n.y, false)
            }
        }
        onWheel: wheel => {
            const factor = wheel.angleDelta.y > 0 ? 1.15 : 1 / 1.15
            const next = Math.min(root.maxZoom, Math.max(root.minZoom, root.zoom * factor))
            if (next !== root.zoom) {
                // Zoom centered on the cursor: keep the point under it fixed.
                const cx = wheel.x - root.width / 2
                const cy = wheel.y - root.height / 2
                root.panX = cx - (cx - root.panX) * (next / root.zoom)
                root.panY = cy - (cy - root.panY) * (next / root.zoom)
                root.zoom = next
                root.clampPan()
            }
            wheel.accepted = true
        }
        onDoubleClicked: {
            if (root.pointerCaptureEnabled)
                return
            if (root.zoom > root.minZoom)
                root.resetZoom()
            else
                root.doubleClicked()
        }
    }
}
