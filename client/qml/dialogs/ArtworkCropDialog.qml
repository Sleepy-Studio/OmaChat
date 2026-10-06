import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

Dialog {
    id: dialog
    objectName: "artworkCropDialog"
    property string channelId
    property string kind: "icon"
    property url fileUrl
    property string accountId: ""
    property string serverId: ""
    property int sourceWidth: 0
    property int sourceHeight: 0
    property string fingerprint: ""
    property string preview: ""
    property string error: ""
    property int requestId: 0
    property bool uploading: false
    protectClose: uploading
    property real focalX: 0.5
    property real focalY: 0.5
    property real zoom: 1
    title: kind === "icon" ? qsTr("Crop channel icon") : qsTr("Crop channel banner")
    width: Math.min(Metrics.px(560), (parent ? parent.width : 800) - Metrics.px(40))
    height: Math.min(Metrics.px(540), (parent ? parent.height : 600) - Metrics.px(40))
    onClosed: { requestId++; preview = "" }
    function openFor(id, artworkKind, url) {
        if (uploading) return
        accountId = App.accountId
        serverId = App.selectedServerId
        sourceWidth = 0
        sourceHeight = 0
        uploading = false
        channelId = id
        kind = artworkKind
        fileUrl = url
        focalX = 0.5
        focalY = 0.5
        zoom = 1
        preview = ""
        fingerprint = ""
        error = ""
        requestId++
        open()
        App.prepareArtworkCrop(requestId, fileUrl, kind)
        viewport.forceActiveFocus()
    }
    Connections {
        target: App
        function onArtworkGenerationChanged() {
            if (dialog.visible) {
                dialog.uploading = false
                dialog.close()
            }
        }
        function onStatusChanged() {
            if (dialog.visible && App.accountId !== dialog.accountId) { dialog.uploading = false; dialog.close() }
        }
        function onSelectionChanged() {
            if (dialog.visible && App.selectedServerId !== dialog.serverId) { dialog.uploading = false; dialog.close() }
        }
        function onAdministrationFinished(operation, id, message, saved) {
            if (operation !== "channel.artwork.crop" || id !== dialog.channelId || !dialog.uploading) return
            dialog.uploading = false
            dialog.error = message
            if (!message) dialog.close()
        }
        function onArtworkCropPrepared(id, url, message, fingerprint, width, height) {
            if (!dialog.visible || id !== dialog.requestId) return
            dialog.sourceWidth = width
            dialog.sourceHeight = height
            dialog.fingerprint = fingerprint
            dialog.preview = url
            dialog.error = message
        }
    }
    contentItem: ColumnLayout {
        spacing: Metrics.px(10)
        Text {
            text: dialog.title
            color: Theme.text
            font.bold: true
            font.pixelSize: Metrics.px(16)
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Drag the image or use arrow keys to position it. Adjust zoom below.")
            wrapMode: Text.Wrap
            color: Theme.textMuted
            font.pixelSize: Metrics.px(12)
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: Metrics.px(48)
            Rectangle {
                id: viewport
                objectName: "artworkCropViewport"
                anchors.centerIn: parent
                width: Math.min(parent.width, dialog.kind === "icon" ? parent.height : parent.height * 4)
                height: width / (dialog.kind === "icon" ? 1 : 4)
                clip: true
                color: Theme.surfaceAlt
                border.color: activeFocus ? Theme.focus : Theme.controlBorder
                border.width: activeFocus ? Metrics.px(2) : Metrics.px(1)
                focus: true
                activeFocusOnTab: true
                Accessible.name: qsTr("Artwork crop position")
                Accessible.description: qsTr("Use arrow keys to move the image; Shift moves faster.")
                Keys.onPressed: event => {
                    if (dialog.uploading) return
                    const step = event.modifiers & Qt.ShiftModifier ? 0.1 : 0.02
                    if (event.key === Qt.Key_Left) dialog.focalX = Math.max(0, dialog.focalX - step)
                    else if (event.key === Qt.Key_Right) dialog.focalX = Math.min(1, dialog.focalX + step)
                    else if (event.key === Qt.Key_Up) dialog.focalY = Math.max(0, dialog.focalY - step)
                    else if (event.key === Qt.Key_Down) dialog.focalY = Math.min(1, dialog.focalY + step)
                    else return
                    event.accepted = true
                }
                Image {
                    id: image
                    source: dialog.preview
                    readonly property real cover: dialog.sourceWidth > 0 && dialog.sourceHeight > 0
                        ? Math.max(viewport.width / dialog.sourceWidth, viewport.height / dialog.sourceHeight) * dialog.zoom : 1
                    width: dialog.sourceWidth * cover
                    height: dialog.sourceHeight * cover
                    x: -(width - viewport.width) * dialog.focalX
                    y: -(height - viewport.height) * dialog.focalY
                    smooth: true
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: !dialog.uploading && dialog.preview.length > 0
                    property real startX
                    property real startY
                    property real initialX
                    property real initialY
                    onPressed: mouse => {
                        viewport.forceActiveFocus()
                        startX = mouse.x; startY = mouse.y
                        initialX = dialog.focalX; initialY = dialog.focalY
                    }
                    onPositionChanged: mouse => {
                        if (!pressed) return
                        const travelX = image.width - viewport.width
                        const travelY = image.height - viewport.height
                        if (travelX > 0) dialog.focalX = Math.max(0, Math.min(1, initialX - (mouse.x - startX) / travelX))
                        if (travelY > 0) dialog.focalY = Math.max(0, Math.min(1, initialY - (mouse.y - startY) / travelY))
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Text { text: qsTr("Zoom"); color: Theme.text; font.pixelSize: Metrics.px(12) }
            Slider {
                objectName: "artworkCropZoom"
                Layout.fillWidth: true
                enabled: !dialog.uploading
                from: 1
                to: 4
                stepSize: 0.05
                value: dialog.zoom
                onMoved: dialog.zoom = value
                Accessible.name: qsTr("Crop zoom")
            }
            FlatButton {
                enabled: !dialog.uploading
                text: qsTr("Reset")
                onClicked: { dialog.focalX = 0.5; dialog.focalY = 0.5; dialog.zoom = 1 }
            }
        }
        Text {
            Layout.fillWidth: true
            visible: dialog.error.length > 0 || dialog.preview.length === 0
            text: dialog.error || qsTr("Preparing image…")
            wrapMode: Text.Wrap
            color: dialog.error ? Theme.danger : Theme.textMuted
            font.pixelSize: Metrics.px(12)
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            FlatButton { enabled: !dialog.uploading; text: qsTr("Cancel"); onClicked: dialog.close() }
            FlatButton {
                objectName: "artworkCropApply"
                text: dialog.uploading ? qsTr("Uploading…") : qsTr("Upload crop")
                primary: true
                enabled: !dialog.uploading && dialog.preview.length > 0 && dialog.fingerprint.length > 0 && App.ready
                onClicked: {
                    dialog.uploading = true
                    dialog.error = ""
                    App.setChannelArtworkCrop(dialog.channelId, dialog.kind, dialog.fileUrl,
                                              dialog.focalX, dialog.focalY, dialog.zoom, dialog.fingerprint)
                }
            }
        }
    }
}
