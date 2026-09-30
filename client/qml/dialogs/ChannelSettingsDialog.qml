import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import OmaChat

Dialog {
    id: dialog
    property string channelId
    property var details: ({})
    title: qsTr("Channel settings")
    width: Math.min(Theme.px(560), (parent ? parent.width : 800) - Theme.px(40))

    function openFor(id) {
        channelId = id
        details = App.channelDetails(id)
        nameField.text = details.name || ""
        topicField.text = details.topic || ""
        descriptionField.text = details.description || ""
        const cats = App.channelCategories()
        let chosen = 0
        for (let i = 0; i < cats.length; ++i)
            if (cats[i].id === details.parent_id)
                chosen = i
        categoryPicker.currentIndex = chosen
        if (details.icon_attachment_id && details.icon_attachment_id !== "0")
            App.requestPreview(details.icon_attachment_id, "channel-icon.png", 0)
        if (details.banner_attachment_id && details.banner_attachment_id !== "0")
            App.requestPreview(details.banner_attachment_id, "channel-banner.png", 0)
        open()
    }

    Connections {
        target: App
        function onChannelDataChanged(id) {
            if (id !== dialog.channelId)
                return
            dialog.details = App.channelDetails(id)
            if (dialog.details.icon_attachment_id && dialog.details.icon_attachment_id !== "0")
                App.requestPreview(dialog.details.icon_attachment_id, "channel-icon.png", 0)
            if (dialog.details.banner_attachment_id && dialog.details.banner_attachment_id !== "0")
                App.requestPreview(dialog.details.banner_attachment_id, "channel-banner.png", 0)
        }
        function onChannelDetailsSaved(id) {
            if (id === dialog.channelId)
                dialog.close()
        }
    }

    FileDialog {
        id: imagePicker
        property string kind
        title: kind === "icon" ? qsTr("Choose channel icon") : qsTr("Choose channel banner")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp)")]
        onAccepted: App.setChannelArtwork(dialog.channelId, kind, selectedFile)
    }

    contentItem: ColumnLayout {
        spacing: Theme.px(12)
        Text {
            text: dialog.title
            color: Theme.text
            font.pixelSize: Theme.px(16)
            font.bold: true
        }
        Field {
            id: nameField
            Layout.fillWidth: true
            label: qsTr("Name")
            input.maximumLength: 64
        }
        ComboBox {
            id: categoryPicker
            Layout.fillWidth: true
            visible: dialog.details.type !== "category"
            model: App.channelCategories()
            textRole: "name"
            valueRole: "id"
            onActivated: App.moveChannelToCategory(dialog.channelId, currentValue)
            Accessible.name: qsTr("Category")
        }
        Field {
            id: topicField
            Layout.fillWidth: true
            label: qsTr("Topic")
            hint: qsTr("Short summary shown in the channel header")
            input.maximumLength: 512
        }
        Text {
            text: qsTr("DESCRIPTION")
            color: Theme.textMuted
            font.pixelSize: Theme.px(11)
            font.bold: true
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.px(150)
            TextArea {
                id: descriptionField
                wrapMode: TextEdit.Wrap
                color: Theme.text
                placeholderText: qsTr("What is this channel for? Add guidance and useful links.")
                font.pixelSize: Theme.px(13)
                background: Rectangle {
                    color: Theme.surfaceAlt
                    radius: Theme.px(4)
                    border.color: descriptionField.activeFocus ? Theme.accent : Theme.border
                }
                Accessible.name: qsTr("Channel description")
            }
        }
        Text {
            Layout.alignment: Qt.AlignRight
            text: descriptionField.length + "/2000"
            color: descriptionField.length > 2000 ? Theme.danger : Theme.textFaint
            font.pixelSize: Theme.px(11)
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.px(8)
            Image {
                source: dialog.details.icon_attachment_id && dialog.details.icon_attachment_id !== "0"
                        ? (App.previews[dialog.details.icon_attachment_id] || "") : ""
                Layout.preferredWidth: Theme.px(36)
                Layout.preferredHeight: Theme.px(36)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }
            FlatButton {
                text: qsTr("Choose icon…")
                onClicked: { imagePicker.kind = "icon"; imagePicker.open() }
            }
            FlatButton {
                text: qsTr("Remove icon")
                enabled: dialog.details.icon_attachment_id && dialog.details.icon_attachment_id !== "0"
                onClicked: App.setChannelArtwork(dialog.channelId, "icon", "")
            }
        }
        Image {
            source: dialog.details.banner_attachment_id && dialog.details.banner_attachment_id !== "0"
                    ? (App.previews[dialog.details.banner_attachment_id] || "") : ""
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.px(76)
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            visible: source.toString().length > 0
        }
        RowLayout {
            Layout.fillWidth: true
            FlatButton {
                text: qsTr("Choose banner…")
                onClicked: { imagePicker.kind = "banner"; imagePicker.open() }
            }
            FlatButton {
                text: qsTr("Remove banner")
                enabled: dialog.details.banner_attachment_id && dialog.details.banner_attachment_id !== "0"
                onClicked: App.setChannelArtwork(dialog.channelId, "banner", "")
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            FlatButton { text: qsTr("Cancel"); onClicked: dialog.close() }
            FlatButton {
                text: qsTr("Save")
                primary: true
                enabled: nameField.text.trim().length > 0 && descriptionField.length <= 2000
                onClicked: {
                    App.updateChannelDetails(dialog.channelId, nameField.text, topicField.text,
                                             descriptionField.text)
                }
            }
        }
    }
}
