import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// Per-channel overrides for roles and members: each permission is denied,
// inherited from the roles, or allowed. Applied category first, then channel;
// a member override wins over role overrides.
Dialog {
    id: dialog

    property string channelId
    property string channelName
    title: qsTr("Permissions for #%1").arg(channelName)
    width: Math.min(Metrics.px(700), (parent ? parent.width : 800) - Metrics.px(40))
    height: Math.min(Metrics.px(580), (parent ? parent.height : 600) - Metrics.px(40))

    // Target being edited: {targetType, targetId, name}
    property var target: null
    property var allow: []
    property var deny: []
    property bool dirty: false

    function openFor(id, name) {
        channelId = id
        channelName = name
        target = null
        dirty = false
        App.loadOverrides(id)
        open()
    }
    function select(t) {
        target = t
        allow = t.allow ? t.allow.slice() : []
        deny = t.deny ? t.deny.slice() : []
        dirty = false
    }
    function valueOf(name) {
        return allow.indexOf(name) >= 0 ? 1 : deny.indexOf(name) >= 0 ? -1 : 0
    }
    function setValue(name, v) {
        allow = allow.filter(p => p !== name)
        deny = deny.filter(p => p !== name)
        if (v > 0)
            allow = allow.concat([name])
        else if (v < 0)
            deny = deny.concat([name])
        dirty = true
    }
    // Targets not yet overridden, for the "Add" picker.
    readonly property var candidates: {
        const taken = {}
        for (const o of App.channelOverrides)
            taken[o.targetType + ":" + o.targetId] = true
        const out = []
        for (const r of App.serverRoles)
            if (!taken["role:" + r.id])
                out.push({ targetType: "role", targetId: r.id, name: r.isDefault ? qsTr("@everyone") : r.name, label: qsTr("Role: %1").arg(r.isDefault ? qsTr("@everyone") : r.name) })
        for (const m of App.serverMembers)
            if (!taken["user:" + m.userId])
                out.push({ targetType: "user", targetId: m.userId, name: m.name, label: qsTr("Member: %1").arg(m.name) })
        return out
    }

    Connections {
        target: App
        function onOverridesChanged() {
            if (!dialog.target || dialog.dirty)
                return
            for (const o of App.channelOverrides)
                if (o.targetType === dialog.target.targetType && o.targetId === dialog.target.targetId) {
                    dialog.select(o)
                    return
                }
        }
    }

    contentItem: ColumnLayout {
        spacing: Metrics.px(10)

        RowLayout {
            Text { text: dialog.title; color: Theme.text; font.pixelSize: Metrics.px(16); font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
            IconButton { iconName: "x"; tip: qsTr("Close"); onClicked: dialog.close() }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Metrics.px(14)

            ColumnLayout {
                Layout.preferredWidth: Metrics.px(220)
                Layout.fillHeight: true
                spacing: Metrics.px(6)

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: App.channelOverrides
                    spacing: Metrics.px(2)
                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        width: ListView.view.width
                        implicitHeight: Metrics.px(32)
                        radius: Metrics.px(4)
                        readonly property bool current: dialog.target !== null && dialog.target.targetType === modelData.targetType
                                                        && dialog.target.targetId === modelData.targetId
                        color: current ? Theme.selection : area.containsMouse ? Theme.raised : "transparent"
                        MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; onClicked: dialog.select(row.modelData) }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Metrics.px(8)
                            anchors.rightMargin: Metrics.px(8)
                            Icon { name: row.modelData.targetType === "user" ? "users" : "shield"; size: Metrics.px(14) }
                            Text {
                                Layout.fillWidth: true
                                text: row.modelData.name
                                color: Theme.text
                                elide: Text.ElideRight
                                font.pixelSize: Metrics.px(13)
                            }
                        }
                    }
                }
                Text {
                    visible: App.channelOverrides.length === 0
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: qsTr("No overrides: this channel follows the server roles.")
                    color: Theme.textFaint
                    font.pixelSize: Metrics.px(12)
                }
                ComboBox {
                    id: picker
                    Layout.fillWidth: true
                    implicitHeight: Metrics.px(32)
                    model: dialog.candidates
                    textRole: "label"
                    displayText: qsTr("Add a role or member…")
                    enabled: App.canManageRoles && dialog.candidates.length > 0
                    font.pixelSize: Metrics.px(13)
                    palette.button: Theme.surfaceAlt
                    palette.buttonText: Theme.text
                    palette.window: Theme.raised
                    palette.text: Theme.text
                    palette.highlight: Theme.selection
                    palette.highlightedText: Theme.text
                    onActivated: index => {
                        const c = dialog.candidates[index]
                        dialog.select({ targetType: c.targetType, targetId: c.targetId, name: c.name, allow: [], deny: [] })
                    }
                }
            }

            Rectangle { Layout.fillHeight: true; implicitWidth: 1; color: Theme.border }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: dialog.target !== null
                spacing: Metrics.px(8)

                Text {
                    text: dialog.target ? dialog.target.name : ""
                    color: Theme.text
                    font.bold: true
                    font.pixelSize: Metrics.px(14)
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    ColumnLayout {
                        width: parent.width
                        spacing: Metrics.px(4)
                        Repeater {
                            // Server-wide permissions make no sense per channel.
                            model: App.permissionCatalog.filter(p => ["KICK_MEMBERS", "BAN_MEMBERS", "MANAGE_SERVER",
                                                                    "MANAGE_ROLES", "ADMINISTRATOR", "CREATE_CHANNEL"].indexOf(p.name) < 0)
                            delegate: RowLayout {
                                id: permRow
                                required property var modelData
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: permRow.modelData.label
                                    color: Theme.text
                                    font.pixelSize: Metrics.px(13)
                                    elide: Text.ElideRight
                                }
                                TriState {
                                    value: dialog.valueOf(permRow.modelData.name)
                                    enabled: App.canManageRoles
                                    onChanged: v => dialog.setValue(permRow.modelData.name, v)
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    FlatButton {
                        text: qsTr("Remove override")
                        danger: true
                        enabled: App.canManageRoles
                        onClicked: {
                            App.setOverride(dialog.channelId, dialog.target.targetType, dialog.target.targetId, [], [])
                            dialog.target = null
                        }
                    }
                    Item { Layout.fillWidth: true }
                    FlatButton {
                        text: qsTr("Save")
                        primary: true
                        enabled: dialog.dirty && App.canManageRoles
                        onClicked: {
                            App.setOverride(dialog.channelId, dialog.target.targetType, dialog.target.targetId,
                                            dialog.allow, dialog.deny)
                            dialog.dirty = false
                        }
                    }
                }
            }
            Text {
                visible: dialog.target === null
                Layout.fillWidth: true
                text: qsTr("Pick an override, or add one")
                color: Theme.textFaint
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
}
