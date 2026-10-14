import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

Dialog {
    id: dialog
    objectName: "artworkCropDialog"
    property string target: "channel"
    signal cropSelected(string kind, url token)
    property string channelId
    property string kind: "icon"
    property url fileUrl
    property var artworkEpoch: 0
    property string artworkScope: ""
    property string accountId: ""
    property string serverId: ""
    property int sourceWidth: 0
    property int sourceHeight: 0
    property string fingerprint: ""
    property string preview: ""
    property string error: ""
    property bool statusIsError: false
    property int requestId: 0
    property bool uploading: false
    property bool fetchingSource: false
    property alias keepSource: keepSourceCheck.checked
    property bool reloading: false
    property bool needsReload: false
    property var originalArtwork: ({})
    property var targetDetails: ({})
    readonly property bool checkedArtworkSupported: App.capabilities.indexOf("channel.artwork.cas.v1") >= 0
    readonly property bool canUpload: App.state === "connected"
        && App.accountId === accountId && App.selectedServerId === serverId
        && (target === "creation" || (target === "server" ? App.canManageServer : (App.canManageChannels && targetDetails.id === channelId && targetDetails.server_id === serverId
            && (targetDetails.permissions || []).indexOf("MANAGE_CHANNEL") >= 0)))
    protectClose: uploading || reloading
    property bool restoreOnPrepare: false
    property real focalX: 0.5
    property real focalY: 0.5
    property real zoom: 1
    title: target === "server"
        ? (kind === "icon" ? qsTr("Crop server icon") : qsTr("Crop server banner"))
        : (kind === "icon" ? qsTr("Crop channel icon") : qsTr("Crop channel banner"))
    width: Math.min(Metrics.px(560), (parent ? parent.width : 800) - Metrics.px(40))
    height: Math.min(Metrics.px(540), (parent ? parent.height : 600) - Metrics.px(40))
    onClosed: { requestId++; preview = ""; fetchingSource = false }
    function artworkBaseline(details) {
        return {icon_attachment_id: details.icon_attachment_id || "0", banner_attachment_id: details.banner_attachment_id || "0"}
    }
    function checkArtwork() {
        if (!visible || target !== "channel") return
        targetDetails = App.channelDetails(channelId)
        if (uploading || reloading || needsReload) return
        const current = artworkBaseline(targetDetails)
        if (current.icon_attachment_id !== originalArtwork.icon_attachment_id
                || current.banner_attachment_id !== originalArtwork.banner_attachment_id) {
            needsReload = true
            error = qsTr("Channel artwork changed. Your crop is retained. Reload artwork and review it before uploading.")
        }
    }
    function reloadArtwork() {
        if (!canUpload || uploading || reloading) return
        reloading = true
        statusIsError = false
        error = qsTr("Reloading artwork…")
        App.reloadChannelDetails(channelId)
    }
    function openFor(id, artworkKind, url) {
        if (uploading || reloading) return
        artworkEpoch = App.artworkGeneration
        artworkScope = App.artworkScopeFingerprint
        accountId = App.accountId
        serverId = App.selectedServerId
        sourceWidth = 0
        sourceHeight = 0
        uploading = false
        keepSource = false
        reloading = false
        needsReload = false
        channelId = id
        targetDetails = App.channelDetails(id)
        originalArtwork = artworkBaseline(targetDetails)
        kind = artworkKind
        fileUrl = url
        restoreOnPrepare = true
        focalX = 0.5
        focalY = 0.5
        zoom = 1
        preview = ""
        fingerprint = ""
        error = ""
        statusIsError = false
        requestId++
        open()
        if (fileUrl.toString().length > 0) App.prepareArtworkCrop(requestId, fileUrl, kind)
        viewport.forceActiveFocus()
    }
    function openExisting(id, artworkKind, attachmentId) {
        openFor(id, artworkKind, "")
        fetchingSource = true
        keepSource = true
        error = qsTr("Loading the retained image…")
        App.requestArtworkSource(requestId, attachmentId)
    }
    Connections {
        target: App.channels
        function onModelReset() { dialog.checkArtwork() }
    }
    Connections {
        target: App
        function onArtworkSourceReady(request, url, framing, message) {
            if (!dialog.visible || !dialog.fetchingSource || request !== dialog.requestId) return
            dialog.fetchingSource = false
            if (message) {
                dialog.error = message
                dialog.statusIsError = true
                return
            }
            dialog.fileUrl = url
            dialog.restoreOnPrepare = false
            dialog.focalX = framing.source_width > framing.width ? framing.x / (framing.source_width - framing.width) : 0.5
            dialog.focalY = framing.source_height > framing.height ? framing.y / (framing.source_height - framing.height) : 0.5
            const ratio = dialog.kind === "icon" ? 1 : 4
            dialog.zoom = Math.max(1, Math.min(4, Math.min(framing.source_width, framing.source_height * ratio) / (framing.width + 0.000001)))
            dialog.error = qsTr("Shared framing restored. Review it before uploading.")
            dialog.requestId++
            App.prepareArtworkCrop(dialog.requestId, dialog.fileUrl, dialog.kind)
        }
        function onChannelDataChanged(id) { if (id === dialog.channelId) dialog.checkArtwork() }
        function onArtworkGenerationChanged() {
            if (!dialog.visible) return
            const current = App.channelDetails(dialog.channelId)
            const sameScopeEpoch = dialog.target === "channel"
                && App.artworkGeneration !== dialog.artworkEpoch && dialog.artworkScope.length > 0
                && App.artworkScopeFingerprint === dialog.artworkScope
                && App.accountId === dialog.accountId && App.selectedServerId === dialog.serverId
                && App.state === "connected" && App.canManageChannels
                && current.id === dialog.channelId && current.server_id === dialog.serverId
                && (current.permissions || []).indexOf("MANAGE_CHANNEL") >= 0
            if (!sameScopeEpoch) {
                dialog.uploading = false
                dialog.reloading = false
                dialog.close()
                return
            }
            const pending = dialog.uploading || dialog.reloading
            dialog.artworkEpoch = App.artworkGeneration
            dialog.targetDetails = current
            dialog.uploading = false
            dialog.reloading = false
            dialog.needsReload = true
            dialog.error = pending
                ? qsTr("Artwork availability changed during the earlier request. It may have completed. Your crop is retained. Reload artwork and review it before uploading again.")
                : qsTr("Artwork availability changed. Your crop is retained. Reload artwork and review it before uploading.")
            // Local pixels remain useful, but their account-bound preparation
            // fingerprint belongs to the earlier cache epoch.
            dialog.fingerprint = ""
            dialog.restoreOnPrepare = false
            dialog.requestId++
            App.prepareArtworkCrop(dialog.requestId, dialog.fileUrl, dialog.kind)
        }
        function onStatusChanged() {
            if (dialog.visible && App.accountId !== dialog.accountId) { dialog.uploading = false; dialog.reloading = false; dialog.close() }
        }
        function onSelectionChanged() {
            if (dialog.visible && App.selectedServerId !== dialog.serverId) { dialog.uploading = false; dialog.reloading = false; dialog.close() }
        }
        function onAdministrationFinished(operation, id, message, saved) {
            if (!dialog.visible) return
            if (operation === "channel.details.reload" && id === dialog.channelId && dialog.reloading) {
                dialog.reloading = false
                dialog.statusIsError = message.length > 0
                if (message) dialog.error = message
                else {
                    dialog.targetDetails = saved
                    dialog.originalArtwork = dialog.artworkBaseline(saved)
                    dialog.needsReload = false
                    dialog.error = qsTr("Current artwork loaded. Your crop is retained. Review it before uploading.")
                }
                return
            }
            if (operation !== dialog.target + ".artwork.crop" || id !== dialog.channelId || !dialog.uploading) return
            dialog.uploading = false
            dialog.statusIsError = message.length > 0
            dialog.error = message
            if (message && saved && saved.conflict) dialog.needsReload = true
            if (!message) dialog.close()
        }
        function onArtworkCropStaged(id, token, message) {
            if (dialog.target !== "creation") return
            if (!dialog.visible || id !== dialog.requestId) {
                if (token.toString().length > 0) App.discardStagedArtwork(token)
                return
            }
            dialog.uploading = false
            dialog.statusIsError = message.length > 0
            dialog.error = message
            if (!message) {
                dialog.cropSelected(dialog.kind, token)
                dialog.close()
            }
        }
        function onArtworkCropPrepared(id, url, message, fingerprint, width, height, remembered) {
            if (!dialog.visible || id !== dialog.requestId) return
            if (dialog.restoreOnPrepare && !message && remembered && remembered.zoom !== undefined) {
                dialog.focalX = remembered.x
                dialog.focalY = remembered.y
                dialog.zoom = remembered.zoom
                dialog.error = dialog.target === "creation"
                    ? qsTr("Previous crop restored on this device. Review it before using the crop.")
                    : qsTr("Previous crop restored on this device. Review it before uploading.")
            }
            dialog.restoreOnPrepare = false
            dialog.sourceWidth = width
            dialog.sourceHeight = height
            dialog.fingerprint = fingerprint
            dialog.preview = url
            if (message || !dialog.needsReload) {
                dialog.statusIsError = message.length > 0
                if (message || !remembered || remembered.zoom === undefined) dialog.error = message
            }
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
            visible: App.capabilities.indexOf("artwork.framing.v1") < 0
            text: qsTr("Drag the image or use arrow keys to position it. Adjust zoom below.")
            wrapMode: Text.Wrap
            color: Theme.textMuted
            font.pixelSize: Metrics.px(12)
        }
        CheckBox {
            id: keepSourceCheck
            objectName: "artworkKeepSource"
            Layout.fillWidth: true
            visible: App.capabilities.indexOf("artwork.framing.v1") >= 0
            enabled: !dialog.uploading && !dialog.reloading && !dialog.fetchingSource
            text: qsTr("Keep the full image for managers to reframe")
            Accessible.description: qsTr("Checked: retain the full image on this server after removing metadata. Unchecked: upload only the crop. Drag or use arrow keys to position the image.")
            contentItem: Text {
                text: keepSourceCheck.text
                leftPadding: keepSourceCheck.indicator.width + keepSourceCheck.spacing
                wrapMode: Text.Wrap
                color: Theme.text
                font.pixelSize: Metrics.px(12)
                verticalAlignment: Text.AlignVCenter
            }
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
                    if (dialog.uploading || dialog.reloading) return
                    const step = event.modifiers & Qt.ShiftModifier ? 0.1 : 0.02
                    if (event.key === Qt.Key_Left) dialog.focalX = Math.max(0, dialog.focalX - step)
                    else if (event.key === Qt.Key_Right) dialog.focalX = Math.min(1, dialog.focalX + step)
                    else if (event.key === Qt.Key_Up) dialog.focalY = Math.max(0, dialog.focalY - step)
                    else if (event.key === Qt.Key_Down) dialog.focalY = Math.min(1, dialog.focalY + step)
                    else return
                    dialog.restoreOnPrepare = false
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
                    enabled: !dialog.uploading && !dialog.reloading && dialog.preview.length > 0
                    property real startX
                    property real startY
                    property real initialX
                    property real initialY
                    onPressed: mouse => {
                        dialog.restoreOnPrepare = false
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
                enabled: !dialog.uploading && !dialog.reloading
                from: 1
                to: 4
                stepSize: 0.05
                value: dialog.zoom
                onMoved: { dialog.restoreOnPrepare = false; dialog.zoom = value }
                Accessible.name: qsTr("Crop zoom")
            }
            FlatButton {
                objectName: "artworkCropReset"
                enabled: !dialog.uploading && !dialog.reloading
                text: qsTr("Reset")
                onClicked: { dialog.restoreOnPrepare = false; dialog.focalX = 0.5; dialog.focalY = 0.5; dialog.zoom = 1 }
            }
        }
        Text {
            Layout.fillWidth: true
            visible: !dialog.canUpload || (dialog.target === "channel" && !dialog.checkedArtworkSupported)
                || dialog.error.length > 0 || dialog.preview.length === 0
            text: !dialog.canUpload ? qsTr("Reconnect with management permission before uploading artwork.")
                : dialog.target === "channel" && !dialog.checkedArtworkSupported
                    ? qsTr("This server needs an update before checked artwork can be saved.")
                    : dialog.error || qsTr("Preparing image…")
            wrapMode: Text.Wrap
            color: dialog.statusIsError || dialog.needsReload ? Theme.danger : Theme.textMuted
            font.pixelSize: Metrics.px(12)
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            FlatButton { enabled: !dialog.uploading && !dialog.reloading; text: qsTr("Cancel"); onClicked: dialog.close() }
            FlatButton {
                objectName: "artworkCropReload"
                visible: dialog.target === "channel" && dialog.needsReload
                text: dialog.reloading ? qsTr("Reloading…") : qsTr("Reload artwork")
                enabled: dialog.canUpload && !dialog.uploading && !dialog.reloading
                onClicked: dialog.reloadArtwork()
            }
            FlatButton {
                objectName: "artworkCropApply"
                text: dialog.target === "creation"
                    ? (dialog.uploading ? qsTr("Preparing crop…") : qsTr("Use crop"))
                    : (dialog.uploading ? qsTr("Uploading…") : qsTr("Upload crop"))
                primary: true
                enabled: !dialog.uploading && !dialog.reloading && !dialog.needsReload
                    && dialog.preview.length > 0 && dialog.fingerprint.length > 0 && dialog.canUpload
                    && (dialog.target !== "channel" || dialog.checkedArtworkSupported)
                onClicked: {
                    dialog.checkArtwork()
                    if (dialog.needsReload || !dialog.canUpload) return
                    dialog.uploading = true
                    dialog.error = ""
                    dialog.statusIsError = false
                    if (dialog.target === "creation")
                        App.stageArtworkCrop(dialog.requestId, dialog.fileUrl, dialog.kind,
                                             dialog.focalX, dialog.focalY, dialog.zoom, dialog.fingerprint, dialog.keepSource)
                    else if (dialog.target === "server")
                        App.setServerArtworkCrop(dialog.channelId, dialog.kind, dialog.fileUrl,
                                                 dialog.focalX, dialog.focalY, dialog.zoom, dialog.fingerprint, dialog.keepSource)
                    else
                        App.setChannelArtworkCrop(dialog.channelId, dialog.kind, dialog.fileUrl,
                                                  dialog.focalX, dialog.focalY, dialog.zoom, dialog.fingerprint, dialog.originalArtwork, dialog.keepSource)
                }
            }
        }
    }
}
