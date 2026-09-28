import QtQuick
import QtQuick.Controls
import OmaChat

// Themed context menu. Items: MenuAction { text; danger; onTriggered }.
Menu {
    id: menu

    topPadding: Theme.px(4)
    bottomPadding: Theme.px(4)

    delegate: MenuItem {
        id: item
        implicitHeight: Theme.px(30)
        implicitWidth: Theme.px(200)
        contentItem: Text {
            leftPadding: Theme.px(8)
            text: item.text
            color: item.action && item.action.danger ? Theme.danger : Theme.text
            font.pixelSize: Theme.px(13)
            verticalAlignment: Text.AlignVCenter
            opacity: item.enabled ? 1 : 0.4
        }
        background: Rectangle {
            radius: Theme.px(3)
            color: item.highlighted ? Theme.selection : "transparent"
        }
    }

    background: Rectangle {
        implicitWidth: Theme.px(200)
        radius: Theme.px(6)
        color: Theme.raised
        border.color: Theme.border
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animationMs }
    }
}
