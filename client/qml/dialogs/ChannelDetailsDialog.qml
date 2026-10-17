import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

Dialog {
    id: dialog
    property string channelId: ""
    property string accountId: ""
    property string serverId: ""
    property var details: ({})
    readonly property string bannerId: details.banner_attachment_id || ""
    readonly property string iconId: details.icon_attachment_id || ""
    signal editRequested(string channelId)
    title: qsTr("Channel details")
    width: Math.min(Metrics.px(520), (parent ? parent.width : 800) - Metrics.px(40))
    height: Math.min(Metrics.px(420), (parent ? parent.height : 600) - Metrics.px(40))

    function validDetails(value) {
        return App.ready && App.accountId === accountId && App.selectedServerId === serverId
            && value.id === channelId && value.server_id === serverId
    }
    function clearContext() {
        details = ({})
        channelId = ""
        accountId = ""
        serverId = ""
        close()
    }
    function refresh() {
        if (!channelId.length) return
        const value = App.channelDetails(channelId)
        if (!validDetails(value)) { clearContext(); return }
        details = value
    }
    function openFor(id) {
        if (visible) return
        details = ({})
        accountId = App.accountId
        serverId = App.selectedServerId
        channelId = id
        refresh()
        if (!channelId.length) return
        loadImages()
        open()
    }
    function loadImages() {
        if (iconId.length && iconId !== "0") App.requestPreview(iconId, "channel-icon.png", 0)
        if (bannerId.length && bannerId !== "0") App.requestPreview(bannerId, "channel-banner.png", 0)
    }
    onClosed: { if (!visible) { details = ({}); channelId = "" } }
    Connections {
        target: App
        function onChannelDataChanged(id) {
            if (id !== dialog.channelId) return
            dialog.refresh()
            if (dialog.channelId.length) dialog.loadImages()
        }
        function onStatusChanged() { dialog.refresh() }
        function onSelectionChanged() { dialog.refresh() }
        function onArtworkGenerationChanged() { dialog.clearContext() }
    }
    Connections {
        target: App.channels
        function onModelReset() { dialog.refresh() }
    }

    contentItem: ColumnLayout {
        spacing: Metrics.px(10)
        ArtworkBanner {
            objectName: "channelDetailsBanner"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(Metrics.px(100), dialog.availableHeight * 0.3)
            Layout.minimumHeight: Layout.preferredHeight
            Layout.maximumHeight: Layout.preferredHeight
            visible: dialog.bannerId.length > 0 && dialog.bannerId !== "0"
            source: App.previews[dialog.bannerId] || ""
            failed: !!App.previewErrors[dialog.bannerId]
            onRetry: App.requestPreview(dialog.bannerId, "channel-banner.png", 0)
        }
        RowLayout {
            Layout.fillWidth: true
            Item {
                Layout.preferredWidth: Metrics.px(28)
                Layout.preferredHeight: Metrics.px(28)
                Icon {
                    objectName: "channelDetailsIconFallback"
                    anchors.centerIn: parent
                    visible: channelIcon.status !== Image.Ready
                    name: dialog.details.type === "voice" ? "speaker" : "hash"
                    size: Metrics.px(24)
                    Accessible.ignored: true
                }
                Image {
                    id: channelIcon
                    objectName: "channelDetailsIcon"
                    anchors.fill: parent
                    visible: status === Image.Ready
                    source: dialog.iconId.length && dialog.iconId !== "0" ? App.previews[dialog.iconId] || "" : ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: false
                    Accessible.ignored: true
                }
            }
            Text {
                objectName: "channelDetailsName"
                Layout.fillWidth: true
                text: dialog.details.name || ""
                textFormat: Text.PlainText
                color: Theme.text
                font.bold: true
                font.pixelSize: Metrics.px(17)
                elide: Text.ElideRight
                Accessible.role: Accessible.Heading
            }
        }
        ScrollView {
            id: textScroll
            objectName: "channelDetailsScroll"
            activeFocusOnTab: true
            Accessible.name: qsTr("Channel topic and description")
            Keys.onPressed: event => {
                const view = contentItem as Flickable
                if (!view) return
                const maximum = Math.max(0, view.contentHeight - view.height)
                if (event.key === Qt.Key_End) view.contentY = maximum
                else if (event.key === Qt.Key_Home) view.contentY = 0
                else if (event.key === Qt.Key_Down) view.contentY = Math.min(maximum, view.contentY + Metrics.px(32))
                else if (event.key === Qt.Key_Up) view.contentY = Math.max(0, view.contentY - view.height)
                else if (event.key === Qt.Key_PageDown) view.contentY = Math.min(maximum, view.contentY + view.height)
                else if (event.key === Qt.Key_PageUp) view.contentY = Math.max(0, view.contentY - view.height)
                else return
                event.accepted = true
            }
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            Column {
                width: textScroll.availableWidth
                spacing: Metrics.px(10)
                Text {
                    objectName: "channelDetailsTopic"
                    visible: text.length > 0
                    width: parent.width
                    text: dialog.details.topic || ""
                    textFormat: Text.PlainText
                    color: Theme.textMuted
                    wrapMode: Text.Wrap
                }
                Text {
                    objectName: "channelDetailsDescription"
                    width: parent.width
                    text: App.renderChannelDescription(dialog.details.description || "")
                    textFormat: Text.RichText
                    color: Theme.text
                    wrapMode: Text.Wrap
                    onLinkActivated: link => App.openLink(link)
                }
            }
        }
        RowLayout {
            objectName: "channelDetailsActions"
            Layout.alignment: Qt.AlignRight
            FlatButton {
                objectName: "channelDetailsEdit"
                visible: App.state === "connected" && App.canManageChannels && App.capabilities.indexOf("channel.identity.v1") >= 0
                text: qsTr("Edit")
                onClicked: {
                    const id = dialog.channelId
                    dialog.refresh()
                    if (!dialog.channelId.length) return
                    dialog.close()
                    dialog.editRequested(id)
                }
            }
            FlatButton { objectName: "channelDetailsClose"; text: qsTr("Close"); onClicked: dialog.close() }
        }
    }
}
