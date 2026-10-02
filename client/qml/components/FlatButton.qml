import QtQuick
import QtQuick.Controls
import OmaChat

// Text button. `primary` uses the accent color; `danger` for destructive actions.
Button {
    id: control

    property bool primary: false
    property bool danger: false

    implicitHeight: Metrics.px(32)
    leftPadding: Metrics.px(14)
    rightPadding: Metrics.px(14)
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    font.pixelSize: Metrics.px(13)
    font.bold: primary

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.primary ? Theme.accentText : (control.danger ? Theme.danger : Theme.text)
        opacity: control.enabled ? 1 : 0.5
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Metrics.px(4)
        color: control.primary ? (control.down ? Qt.darker(Theme.accent, 1.2) : control.hovered ? Qt.lighter(Theme.accent, 1.1) : Theme.accent)
                               : (control.down ? Theme.selection : control.hovered ? Theme.raised : Theme.surfaceAlt)
        opacity: control.enabled ? 1 : 0.5
        border.width: control.visualFocus ? 2 : (control.primary ? 0 : 1)
        border.color: control.visualFocus ? Theme.text : Theme.border
    }
}
