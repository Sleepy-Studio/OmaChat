import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import OmaChat

Dialog {
    id: dialog
    property bool advanced: false
    property bool saving: false
    property string saveError: ""
    protectClose: saving
    property string parentId
    property url iconFile
    property url bannerFile
    title: qsTr("Create channel")
    width: Math.min(Metrics.px(500), (parent ? parent.width : 800) - Metrics.px(40))
    padding: Metrics.px(12)
    height: Math.min(Metrics.px(700), (parent ? parent.height : 800) - Metrics.px(24))

    onAboutToShow: {
        advanced = false
        saving = false
        saveError = ""
        nameField.text = ""
        topicField.text = ""
        descriptionField.text = ""
        iconFile = ""
        bannerFile = ""
        textType.checked = true
        const categories = App.channelCategories()
        categoryPicker.currentIndex = 0
        for (let i = 0; i < categories.length; ++i)
            if (categories[i].id === dialog.parentId)
                categoryPicker.currentIndex = i
    }
    onOpened: nameField.input.forceActiveFocus()

    function create() {
        const name = nameField.text.trim()
        if (saving || !name || descriptionField.length > 2000)
            return
        const type = voiceType.checked ? "voice" : categoryType.checked ? "category" : "text"
        saving = true
        saveError = ""
        App.createChannel(name, type, type === "category" ? "0" : categoryPicker.currentValue,
                          topicField.text, descriptionField.text, iconFile, bannerFile)
    }

    Connections {
        target: App
        function onAdministrationFinished(operation, id, error) {
            if (operation !== "channel.create" || !dialog.saving) return
            dialog.saving = false
            dialog.saveError = error
            if (!error) dialog.close()
        }
    }

    FileDialog {
        id: artworkPicker
        property string kind
        title: kind === "icon" ? qsTr("Choose channel icon") : qsTr("Choose channel banner")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp)")]
        onAccepted: {
            if (kind === "icon") dialog.iconFile = selectedFile
            else dialog.bannerFile = selectedFile
        }
    }

    contentItem: ColumnLayout {
        spacing: Metrics.px(8)
        Text { text: dialog.title; color: Theme.text; font.pixelSize: Metrics.px(16); font.bold: true }
        ScrollView {
            id: formScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            clip: true
            enabled: !dialog.saving
            ColumnLayout {
                width: formScroll.availableWidth
                spacing: Metrics.px(8)
                RowLayout {
                    Layout.fillWidth: true
                    RadioButton { id: textType; text: qsTr("Text"); checked: true }
                    RadioButton { id: voiceType; text: qsTr("Voice") }
                    RadioButton { id: categoryType; text: qsTr("Category") }
                }
                Field {
                    id: nameField
                    objectName: "channelName"
                    Layout.fillWidth: true
                    label: qsTr("Name")
                    placeholder: voiceType.checked ? qsTr("Lounge") : categoryType.checked ? qsTr("Projects") : qsTr("new-channel")
                    input.maximumLength: 64
                    input.onAccepted: dialog.create()
                }
                ComboBox {
                    id: categoryPicker
                    Layout.fillWidth: true
                    visible: !categoryType.checked
                    model: App.channelCategories()
                    textRole: "name"
                    valueRole: "id"
                    Accessible.name: qsTr("Category")
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: dialog.advanced
                    spacing: Metrics.px(8)
                    Field {
                        id: topicField
                        objectName: "channelTopic"
                        Layout.fillWidth: true
                        label: qsTr("Topic")
                        hint: qsTr("Short summary shown in the header")
                        input.maximumLength: 512
                    }
                    Text { text: qsTr("DESCRIPTION"); color: Theme.textMuted; font.pixelSize: Metrics.px(11); font.bold: true }
                    TextArea {
                        id: descriptionField
                        Layout.fillWidth: true
                        Layout.preferredHeight: Metrics.px(100)
                        wrapMode: TextEdit.Wrap
                        color: Theme.text
                        placeholderText: qsTr("What is this channel for?")
                        font.pixelSize: Metrics.px(13)
                        background: Rectangle {
                            color: Theme.surfaceAlt
                            radius: Metrics.px(4)
                            border.color: descriptionField.activeFocus ? Theme.focus : Theme.controlBorder
                        }
                        Accessible.name: qsTr("Channel description")
                    }
                    Text {
                        Layout.alignment: Qt.AlignRight
                        text: descriptionField.length + "/2000"
                        color: descriptionField.length > 2000 ? Theme.danger : Theme.textFaint
                        font.pixelSize: Metrics.px(11)
                    }
                    RowLayout {
                        FlatButton { text: qsTr("Choose icon…"); onClicked: { artworkPicker.kind = "icon"; artworkPicker.open() } }
                        Text { text: dialog.iconFile.toString().length > 0 ? qsTr("Selected") : ""; color: Theme.textMuted }
                        FlatButton { text: qsTr("Clear"); visible: dialog.iconFile.toString().length > 0; onClicked: dialog.iconFile = "" }
                    }
                    RowLayout {
                        FlatButton { text: qsTr("Choose banner…"); onClicked: { artworkPicker.kind = "banner"; artworkPicker.open() } }
                        Text { text: dialog.bannerFile.toString().length > 0 ? qsTr("Selected") : ""; color: Theme.textMuted }
                        FlatButton { text: qsTr("Clear"); visible: dialog.bannerFile.toString().length > 0; onClicked: dialog.bannerFile = "" }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Access follows this server's roles and channel permissions. Review it in Channel permissions after creation.")
                        wrapMode: Text.Wrap
                        color: Theme.textMuted
                        font.pixelSize: Metrics.px(11)
                    }
                }

            }
        }
        FlatButton {
            objectName: "advancedOptions"
            text: dialog.advanced ? qsTr("Hide optional settings") : qsTr("Optional settings…")
            onClicked: dialog.advanced = !dialog.advanced
        }
        Text {
            Layout.fillWidth: true
            visible: dialog.saveError.length > 0
            text: dialog.saveError
            color: Theme.danger
            wrapMode: Text.Wrap
            font.pixelSize: Metrics.px(13)
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Metrics.px(8)
            FlatButton { text: qsTr("Cancel"); enabled: !dialog.saving; onClicked: dialog.requestClose() }
            FlatButton {
                primary: true
                text: dialog.saving ? qsTr("Creating…") : qsTr("Create")
                enabled: !dialog.saving && nameField.text.trim().length > 0 && descriptionField.length <= 2000
                onClicked: dialog.create()
            }
        }
    }
}
