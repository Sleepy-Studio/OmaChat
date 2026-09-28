import QtQuick
import QtQuick.Layouts
import OmaChat

// Single text input dialog (create server, join invite, topic, ...).
Dialog {
    id: dialog

    property string label
    property string placeholder
    property string acceptText: qsTr("OK")
    property bool allowEmpty: false
    property alias value: field.text
    signal accepted(string value)

    onAboutToShow: field.text = ""
    onOpened: field.input.forceActiveFocus()

    function accept() {
        if (!allowEmpty && field.text.trim().length === 0)
            return
        close()
        accepted(field.text.trim())
    }

    contentItem: ColumnLayout {
        spacing: Theme.px(14)
        Text {
            text: dialog.title
            color: Theme.text
            font.pixelSize: Theme.px(16)
            font.bold: true
        }
        Field {
            id: field
            Layout.fillWidth: true
            label: dialog.label
            placeholder: dialog.placeholder
            input.onAccepted: dialog.accept()
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.px(8)
            FlatButton { text: qsTr("Cancel"); onClicked: dialog.close() }
            FlatButton {
                primary: true
                text: dialog.acceptText
                enabled: dialog.allowEmpty || field.text.trim().length > 0
                onClicked: dialog.accept()
            }
        }
    }
}
