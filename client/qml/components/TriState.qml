import QtQuick
import QtQuick.Layouts
import OmaChat

// Deny / inherit / allow switch for a channel permission override.
// value: -1 deny, 0 inherit, 1 allow.
RowLayout {
    id: root

    property int value: 0
    signal changed(int value)

    spacing: 0

    Repeater {
        id: segments
        model: [{ v: -1, label: "✕", tip: qsTr("Deny") }, { v: 0, label: "/", tip: qsTr("Inherit") },
                { v: 1, label: "✓", tip: qsTr("Allow") }]
        delegate: Rectangle {
            id: seg
            required property var modelData
            required property int index
            activeFocusOnTab: root.enabled
            function activate() {
                if (!root.enabled) return
                root.value = modelData.v
                root.changed(modelData.v)
            }
            Keys.onSpacePressed: activate()
            Keys.onReturnPressed: activate()
            Keys.onEnterPressed: activate()
            Keys.onLeftPressed: {
                if (index > 0) segments.itemAt(index - 1).forceActiveFocus(Qt.TabFocusReason)
            }
            Keys.onRightPressed: {
                if (index < 2) segments.itemAt(index + 1).forceActiveFocus(Qt.TabFocusReason)
            }
            readonly property bool active: root.value === modelData.v
            implicitWidth: Metrics.px(28)
            implicitHeight: Metrics.px(22)
            color: !active ? Theme.surfaceAlt
                 : modelData.v < 0 ? Theme.danger
                 : modelData.v > 0 ? Theme.success : Theme.selection
            border.color: Theme.controlBorder
            border.width: 1
            Rectangle {
                anchors.fill: parent
                anchors.margins: -Metrics.px(2)
                color: "transparent"
                border.width: Metrics.px(2)
                border.color: Theme.focus
                visible: seg.activeFocus && root.enabled
                z: 1
            }
            opacity: root.enabled ? 1 : 0.5
            Text {
                anchors.centerIn: parent
                text: seg.modelData.label
                color: !seg.active || seg.modelData.v === 0 ? Theme.textMuted
                     : seg.modelData.v < 0 ? Theme.dangerText : Theme.successText
                font.pixelSize: Metrics.px(12)
                font.bold: seg.active
            }
            MouseArea {
                anchors.fill: parent
                enabled: root.enabled
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    seg.forceActiveFocus(Qt.MouseFocusReason)
                    seg.activate()
                }
            }
            Accessible.role: Accessible.RadioButton
            Accessible.name: seg.modelData.tip
            Accessible.checked: seg.active
            Accessible.onPressAction: activate()
        }
    }
}
