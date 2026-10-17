import QtQuick
import QtQuick.Controls
import OmaChat

// Themed context menu. Items: MenuAction { text; danger; onTriggered }.
Menu {
    id: menu

    topPadding: Metrics.px(4)
    bottomPadding: Metrics.px(4)

    delegate: MenuItem {
        id: item
        implicitHeight: Metrics.px(30)
        implicitWidth: Metrics.px(200)
        contentItem: Text {
            leftPadding: Metrics.px(8)
            text: (item.checkable && item.checked ? "✓ " : "") + item.text
            color: item.action && item.action.danger ? Theme.danger : Theme.text
            font.pixelSize: Metrics.px(13)
            verticalAlignment: Text.AlignVCenter
            opacity: item.enabled ? 1 : 0.4
        }
        background: Rectangle {
            radius: Metrics.px(3)
            color: item.highlighted ? Theme.selection : "transparent"
            border.width: item.visualFocus ? Metrics.px(2) : 0
            border.color: Theme.focus
        }
    }

    background: Rectangle {
        implicitWidth: Metrics.px(200)
        radius: Metrics.px(6)
        color: Theme.raised
        border.color: Theme.border
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animationMs }
    }
}
