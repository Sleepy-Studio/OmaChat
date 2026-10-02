import QtQuick
import QtQuick.Controls
import OmaChat

// Base modal: centered, themed, closes on Esc and outside click, returns
// focus to where it was.
Popup {
    id: dialog

    property string title

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: Metrics.px(20)
    width: Math.min(Metrics.px(460), (parent ? parent.width : 800) - Metrics.px(40))


    background: Rectangle {
        radius: Metrics.px(8)
        color: Theme.surface
        border.color: Theme.border
    }

    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.55) }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animationMs }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.animationMs }
    }
}
