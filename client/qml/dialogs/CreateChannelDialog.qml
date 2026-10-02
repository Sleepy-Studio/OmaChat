import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import OmaChat

Dialog {
    id: dialog
    property string parentId
    property url iconFile
    property url bannerFile
    title: qsTr("Create channel")
    width: Math.min(Metrics.px(500), (parent ? parent.width : 800) - Metrics.px(40))
    height: Math.min(Metrics.px(700), (parent ? parent.height : 800) - Metrics.px(40))

    onAboutToShow: {
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
        if (!name || descriptionField.length > 2000)
            return
        const type = voiceType.checked ? "voice" : categoryType.checked ? "category" : "text"
        App.createChannel(name, type, type === "category" ? "0" : categoryPicker.currentValue,
                          topicField.text, descriptionField.text, iconFile, bannerFile)
        close()
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

    component TypeOption: RadioButton {
        id: option
        property string hint
        Layout.fillWidth: true
        contentItem: Column {
            leftPadding: option.indicator.width + Metrics.px(8)
            Text { text: option.text; color: Theme.text; font.pixelSize: Metrics.px(14) }
            Text { text: option.hint; color: Theme.textFaint; font.pixelSize: Metrics.px(11) }
        }
    }

    contentItem: ColumnLayout {
        spacing: Metrics.px(10)
        Text { text: dialog.title; color: Theme.text; font.pixelSize: Metrics.px(16); font.bold: true }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ColumnLayout {
                width: parent.width
                spacing: Metrics.px(10)
                TypeOption { id: textType; text: qsTr("Text"); hint: qsTr("Messages, links and code"); checked: true }
                TypeOption { id: voiceType; text: qsTr("Voice"); hint: qsTr("Low-latency voice chat") }
                TypeOption { id: categoryType; text: qsTr("Category"); hint: qsTr("Groups channels together") }
                Field {
                    id: nameField
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
                Field {
                    id: topicField
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
                        border.color: descriptionField.activeFocus ? Theme.accent : Theme.border
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
                    Text { text: dialog.iconFile.toString().length > 0 ? qsTr("Selected") : ""; color: Theme.textFaint }
                    FlatButton { text: qsTr("Clear"); visible: dialog.iconFile.toString().length > 0; onClicked: dialog.iconFile = "" }
                }
                RowLayout {
                    FlatButton { text: qsTr("Choose banner…"); onClicked: { artworkPicker.kind = "banner"; artworkPicker.open() } }
                    Text { text: dialog.bannerFile.toString().length > 0 ? qsTr("Selected") : ""; color: Theme.textFaint }
                    FlatButton { text: qsTr("Clear"); visible: dialog.bannerFile.toString().length > 0; onClicked: dialog.bannerFile = "" }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Access follows this server's roles and channel permissions. Review it in Channel permissions after creation.")
                    wrapMode: Text.Wrap
                    color: Theme.textFaint
                    font.pixelSize: Metrics.px(11)
                }
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Metrics.px(8)
            FlatButton { text: qsTr("Cancel"); onClicked: dialog.close() }
            FlatButton {
                primary: true
                text: qsTr("Create")
                enabled: nameField.text.trim().length > 0 && descriptionField.length <= 2000
                onClicked: dialog.create()
            }
        }
    }
}
