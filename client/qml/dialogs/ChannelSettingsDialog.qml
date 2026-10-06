import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import OmaChat

Dialog {
    id: dialog
    property var original: ({})
    property var submitted: ({})
    property bool saving: false
    property string saveStatus: ""
    property bool saveFailed: false
    readonly property bool dirty: nameField.text !== (original.name || "")
        || topicField.text !== (original.topic || "")
        || descriptionField.text !== (original.description || "")
    protectClose: dirty || saving
    onCloseRequested: { if (!saving) discardConfirm.open() }
    height: Math.min(Metrics.px(740), (parent ? parent.height : 800) - Metrics.px(40))
    property string channelId
    property var details: ({})
    title: qsTr("Channel settings")
    width: Math.min(Metrics.px(560), (parent ? parent.width : 800) - Metrics.px(40))

    function openFor(id) {
        if (visible) return
        saving = false
        saveStatus = ""
        saveFailed = false
        channelId = id
        details = App.channelDetails(id)
        original = details
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
            const keepInput = dialog.dirty || dialog.saving
            dialog.details = App.channelDetails(id)
            if (!keepInput) {
                dialog.original = dialog.details
                nameField.text = dialog.details.name || ""
                topicField.text = dialog.details.topic || ""
                descriptionField.text = dialog.details.description || ""
            }
            if (dialog.details.icon_attachment_id && dialog.details.icon_attachment_id !== "0")
                App.requestPreview(dialog.details.icon_attachment_id, "channel-icon.png", 0)
            if (dialog.details.banner_attachment_id && dialog.details.banner_attachment_id !== "0")
                App.requestPreview(dialog.details.banner_attachment_id, "channel-banner.png", 0)
        }
        function onAdministrationFinished(operation, id, error, saved) {
            if (operation !== "channel.update" || id !== dialog.channelId || !dialog.saving) return
            dialog.saving = false
            dialog.saveFailed = error.length > 0
            dialog.saveStatus = error || qsTr("Channel details saved.")
            if (!error) {
                if (nameField.text === dialog.submitted.name) nameField.text = saved.name || ""
                if (topicField.text === dialog.submitted.topic) topicField.text = saved.topic || ""
                if (descriptionField.text === dialog.submitted.description) descriptionField.text = saved.description || ""
                dialog.original = saved
            }
        }
    }

    ConfirmDialog {
        id: discardConfirm
        onClosed: { if (dialog.visible) dialog.contentItem.forceActiveFocus() }
        objectName: "discardChannel"
        title: qsTr("Discard channel edits?")
        message: qsTr("Your unsaved name, topic and description will be lost.")
        confirmText: qsTr("Discard edits")
        destructive: true
        onConfirmed: dialog.close()
    }

    FileDialog {
        id: imagePicker
        property string kind
        title: kind === "icon" ? qsTr("Choose channel icon") : qsTr("Choose channel banner")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp)")]
        onAccepted: artworkCrop.openFor(dialog.channelId, kind, selectedFile)
    }

    ArtworkCropDialog {
        id: artworkCrop
        onClosed: { if (dialog.visible) dialog.contentItem.forceActiveFocus() }
    }

    contentItem: ColumnLayout {
        spacing: Metrics.px(12)
        Text {
            text: dialog.title
            color: Theme.text
            font.pixelSize: Metrics.px(16)
            font.bold: true
        }
        ScrollView {
            id: formScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            clip: true
            enabled: !dialog.saving
            ColumnLayout {
                width: formScroll.availableWidth
                spacing: Metrics.px(12)
                Field {
                    id: nameField
                    objectName: "channelName"
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
                    font.pixelSize: Metrics.px(11)
                    font.bold: true
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Metrics.px(150)
                    TextArea {
                        id: descriptionField
                        wrapMode: TextEdit.Wrap
                        color: Theme.text
                        placeholderText: qsTr("What is this channel for? Add guidance and useful links.")
                        font.pixelSize: Metrics.px(13)
                        background: Rectangle {
                            color: Theme.surfaceAlt
                            radius: Metrics.px(4)
                            border.color: descriptionField.activeFocus ? Theme.focus : Theme.controlBorder
                        }
                        Accessible.name: qsTr("Channel description")
                    }
                }
                Text {
                    Layout.alignment: Qt.AlignRight
                    text: descriptionField.length + "/2000"
                    color: descriptionField.length > 2000 ? Theme.danger : Theme.textFaint
                    font.pixelSize: Metrics.px(11)
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Metrics.px(8)
                    Image {
                        source: dialog.details.icon_attachment_id && dialog.details.icon_attachment_id !== "0"
                                ? (App.previews[dialog.details.icon_attachment_id] || "") : ""
                        Layout.preferredWidth: Metrics.px(36)
                        Layout.preferredHeight: Metrics.px(36)
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                    FlatButton {
                        text: qsTr("Choose icon…")
                        onClicked: { imagePicker.kind = "icon"; imagePicker.open() }
                    }
                    FlatButton {
                        text: qsTr("Remove icon")
                        enabled: !!dialog.details.icon_attachment_id && dialog.details.icon_attachment_id !== "0"
                        onClicked: App.setChannelArtwork(dialog.channelId, "icon", "")
                    }
                }
                Image {
                    source: dialog.details.banner_attachment_id && dialog.details.banner_attachment_id !== "0"
                            ? (App.previews[dialog.details.banner_attachment_id] || "") : ""
                    Layout.fillWidth: true
                    Layout.preferredHeight: Metrics.px(76)
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
                        enabled: !!dialog.details.banner_attachment_id && dialog.details.banner_attachment_id !== "0"
                        onClicked: App.setChannelArtwork(dialog.channelId, "banner", "")
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Category and artwork changes apply immediately. Save applies to name, topic and description.")
                    wrapMode: Text.Wrap
                    color: Theme.textMuted
                    font.pixelSize: Metrics.px(12)
                }
            }
        }
        Text {
            Layout.fillWidth: true
            visible: dialog.saveStatus.length > 0
            text: dialog.saveStatus
            wrapMode: Text.Wrap
            color: dialog.saveFailed ? Theme.danger : Theme.textMuted
            font.pixelSize: Metrics.px(13)
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            FlatButton { text: qsTr("Close"); enabled: !dialog.saving; onClicked: dialog.requestClose() }
            FlatButton {
                text: dialog.saving ? qsTr("Saving…") : qsTr("Save")
                primary: true
                enabled: dialog.dirty && !dialog.saving && nameField.text.trim().length > 0 && descriptionField.length <= 2000
                onClicked: {
                    dialog.submitted = {name: nameField.text, topic: topicField.text, description: descriptionField.text}
                    dialog.saving = true
                    dialog.saveFailed = false
                    dialog.saveStatus = qsTr("Saving channel details…")
                    App.updateChannelDetails(dialog.channelId, nameField.text, topicField.text,
                                             descriptionField.text)
                }
            }
        }
    }
}
