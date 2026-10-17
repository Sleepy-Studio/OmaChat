import QtQuick
import QtQuick.Controls
import OmaChat

// Square icon button with hover, pressed, checked and keyboard focus states.
AbstractButton {
    id: control

    property string iconName
    property string tip
    property color iconColor: checked ? Theme.danger : (hovered ? Theme.text : Theme.textMuted)
    property real iconSize: Metrics.px(18)
    property bool danger: false

    implicitWidth: Metrics.px(32)
    implicitHeight: Metrics.px(32)
    focusPolicy: Qt.TabFocus
    hoverEnabled: true

    Accessible.role: Accessible.Button
    Accessible.name: tip
    Accessible.checkable: checkable
    Accessible.checked: checked

    ToolTip.visible: hovered && tip.length > 0
    ToolTip.delay: 600
    ToolTip.text: tip

    background: Rectangle {
        radius: Metrics.px(4)
        color: control.down ? Theme.selection : (control.hovered ? Theme.raised : "transparent")
        border.width: control.visualFocus ? 2 : 0
        border.color: Theme.focus
    }

    contentItem: Item {
        opacity: control.enabled ? 1 : 0.5
        Icon {
            anchors.centerIn: parent
            name: control.iconName
            size: control.iconSize
            color: control.danger ? Theme.danger : control.iconColor
        }
    }
}
