import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

Dialog {
    id: dialog
    property string channelId
    property var details: ({})
    signal editRequested(string channelId)
    title: details.name || qsTr("Channel details")
    width: Math.min(Theme.px(520), (parent ? parent.width : 800) - Theme.px(40))
    height: Math.min(Theme.px(380), (parent ? parent.height : 600) - Theme.px(40))

    function openFor(id) {
        channelId = id
        details = App.channelDetails(id)
        loadImages()
        open()
    }
    function loadImages() {
        if (details.icon_attachment_id && details.icon_attachment_id !== "0")
            App.requestPreview(details.icon_attachment_id, "channel-icon.png", 0)
        if (details.banner_attachment_id && details.banner_attachment_id !== "0")
            App.requestPreview(details.banner_attachment_id, "channel-banner.png", 0)
    }

    Connections {
        target: App
        function onChannelDataChanged(id) {
            if (id !== dialog.channelId)
                return
            dialog.details = App.channelDetails(id)
            dialog.loadImages()
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.px(10)
        Image {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.px(130)
            visible: source.toString().length > 0 && status === Image.Ready
            source: App.previews[dialog.details.banner_attachment_id] || ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
        }
        RowLayout {
            Layout.fillWidth: true
            Image {
                visible: source.toString().length > 0 && status === Image.Ready
                source: App.previews[dialog.details.icon_attachment_id] || ""
                Layout.preferredWidth: Theme.px(28)
                Layout.preferredHeight: Theme.px(28)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }
            Text {
                Layout.fillWidth: true
                text: dialog.details.name || ""
                color: Theme.text
                font.bold: true
                font.pixelSize: Theme.px(17)
                elide: Text.ElideRight
                Accessible.role: Accessible.Heading
            }
        }
        Text {
            visible: (dialog.details.topic || "").length > 0
            Layout.fillWidth: true
            text: dialog.details.topic || ""
            color: Theme.textMuted
            wrapMode: Text.Wrap
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            Text {
                width: parent.width
                text: App.renderChannelDescription(dialog.details.description || "")
                textFormat: Text.RichText
                color: Theme.text
                wrapMode: Text.Wrap
                onLinkActivated: link => App.openLink(link)
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            FlatButton {
                visible: App.canManageChannels && App.capabilities.indexOf("channel.identity.v1") >= 0
                text: qsTr("Edit")
                onClicked: { dialog.close(); dialog.editRequested(dialog.channelId) }
            }
            FlatButton { text: qsTr("Close"); onClicked: dialog.close() }
        }
    }
}
