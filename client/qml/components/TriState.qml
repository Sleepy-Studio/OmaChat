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
        model: [{ v: -1, label: "✕", tip: qsTr("Deny") }, { v: 0, label: "/", tip: qsTr("Inherit") },
                { v: 1, label: "✓", tip: qsTr("Allow") }]
        delegate: Rectangle {
            id: seg
            required property var modelData
            readonly property bool active: root.value === modelData.v
            implicitWidth: Theme.px(28)
            implicitHeight: Theme.px(22)
            color: !active ? Theme.surfaceAlt
                 : modelData.v < 0 ? Theme.danger
                 : modelData.v > 0 ? Theme.success : Theme.selection
            border.color: Theme.border
            opacity: root.enabled ? 1 : 0.5
            Text {
                anchors.centerIn: parent
                text: seg.modelData.label
                color: seg.active && seg.modelData.v !== 0 ? Theme.accentText : Theme.textMuted
                font.pixelSize: Theme.px(12)
                font.bold: seg.active
            }
            MouseArea {
                anchors.fill: parent
                enabled: root.enabled
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    root.value = seg.modelData.v
                    root.changed(seg.modelData.v)
                }
            }
            Accessible.role: Accessible.RadioButton
            Accessible.name: seg.modelData.tip
            Accessible.checked: seg.active
        }
    }
}
