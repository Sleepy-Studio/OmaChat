import QtQuick
import QtQuick.Controls
import OmaChat

// Single-line input with label.
Column {
    id: root

    property alias text: input.text
    property alias placeholder: input.placeholderText
    property alias echoMode: input.echoMode
    property alias input: input
    property string label
    property string hint

    spacing: Metrics.px(4)

    Text {
        visible: root.label.length > 0
        text: root.label.toUpperCase()
        color: Theme.textMuted
        font.pixelSize: Metrics.px(11)
        font.bold: true
        font.letterSpacing: 0.4
    }

    TextField {
        id: input
        width: root.width
        implicitHeight: Metrics.px(34)
        color: Theme.text
        placeholderTextColor: Theme.textFaint
        selectionColor: Theme.accent
        selectedTextColor: Theme.accentText
        font.pixelSize: Metrics.px(14)
        leftPadding: Metrics.px(10)
        Accessible.name: root.label
        background: Rectangle {
            radius: Metrics.px(4)
            color: Theme.surfaceAlt
            border.width: input.activeFocus ? 2 : 1
            border.color: input.activeFocus ? Theme.accent : Theme.border
        }
    }

    Text {
        visible: root.hint.length > 0
        width: root.width
        text: root.hint
        wrapMode: Text.Wrap
        color: Theme.textFaint
        font.pixelSize: Metrics.px(11)
    }
}
