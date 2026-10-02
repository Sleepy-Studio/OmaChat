import QtQuick
import QtQuick.Layouts
import OmaChat

Dialog {
    id: dialog

    property string message
    property string confirmText: qsTr("OK")
    property bool destructive: false
    signal confirmed()

    onOpened: cancelButton.forceActiveFocus()

    contentItem: ColumnLayout {
        spacing: Metrics.px(14)
        Text {
            Layout.fillWidth: true
            text: dialog.title
            color: Theme.text
            font.pixelSize: Metrics.px(16)
            font.bold: true
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            text: dialog.message
            color: Theme.textMuted
            font.pixelSize: Metrics.px(13)
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Metrics.px(8)
            FlatButton {
                id: cancelButton
                text: qsTr("Cancel")
                onClicked: dialog.close()
                Keys.onReturnPressed: dialog.close()
            }
            FlatButton {
                primary: !dialog.destructive
                danger: dialog.destructive
                text: dialog.confirmText
                onClicked: {
                    dialog.close()
                    dialog.confirmed()
                }
                Keys.onReturnPressed: clicked()
            }
        }
    }
}
