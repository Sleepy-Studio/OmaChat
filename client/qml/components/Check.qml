import QtQuick
import QtQuick.Controls
import OmaChat

// Themed checkbox with an optional second line of explanation.
CheckBox {
    id: control

    property string description

    font.pixelSize: Theme.px(13)
    spacing: Theme.px(8)
    padding: Theme.px(2)

    indicator: Rectangle {
        x: control.leftPadding
        y: control.topPadding + Theme.px(1)
        implicitWidth: Theme.px(16)
        implicitHeight: Theme.px(16)
        radius: Theme.px(3)
        color: control.checked ? Theme.accent : Theme.surfaceAlt
        border.color: control.visualFocus ? Theme.text : (control.checked ? Theme.accent : Theme.border)
        border.width: control.visualFocus ? 2 : 1
        opacity: control.enabled ? 1 : 0.5
        Text {
            anchors.centerIn: parent
            visible: control.checked
            text: "✓"
            color: Theme.accentText
            font.pixelSize: Theme.px(11)
            font.bold: true
        }
    }

    contentItem: Column {
        leftPadding: control.indicator.width + control.spacing
        opacity: control.enabled ? 1 : 0.5
        Text {
            text: control.text
            color: Theme.text
            font: control.font
        }
        Text {
            visible: control.description.length > 0
            text: control.description
            color: Theme.textFaint
            font.pixelSize: Theme.px(11)
        }
    }
}
