import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

Dialog {
    id: dialog

    property string parentId
    title: qsTr("Create channel")

    onAboutToShow: {
        nameField.text = ""
        textType.checked = true
    }
    onOpened: nameField.input.forceActiveFocus()

    function accept() {
        const name = nameField.text.trim()
        if (name.length === 0)
            return
        close()
        App.createChannel(name, voiceType.checked ? "voice" : categoryType.checked ? "category" : "text",
                          categoryType.checked ? "" : dialog.parentId)
    }

    component TypeOption: RadioButton {
        id: option
        property string hint
        Layout.fillWidth: true
        contentItem: Column {
            leftPadding: option.indicator.width + Theme.px(8)
            Text { text: option.text; color: Theme.text; font.pixelSize: Theme.px(14) }
            Text { text: option.hint; color: Theme.textFaint; font.pixelSize: Theme.px(11) }
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.px(10)
        Text {
            text: dialog.title
            color: Theme.text
            font.pixelSize: Theme.px(16)
            font.bold: true
        }
        TypeOption { id: textType; text: qsTr("Text"); hint: qsTr("Messages, links and code"); checked: true }
        TypeOption { id: voiceType; text: qsTr("Voice"); hint: qsTr("Low-latency voice chat") }
        TypeOption { id: categoryType; text: qsTr("Category"); hint: qsTr("Groups channels together") }
        Field {
            id: nameField
            Layout.fillWidth: true
            label: qsTr("Name")
            placeholder: voiceType.checked ? qsTr("Lounge") : categoryType.checked ? qsTr("Projects") : qsTr("new-channel")
            input.onAccepted: dialog.accept()
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.px(8)
            FlatButton { text: qsTr("Cancel"); onClicked: dialog.close() }
            FlatButton { primary: true; text: qsTr("Create"); enabled: nameField.text.trim().length > 0; onClicked: dialog.accept() }
        }
    }
}
