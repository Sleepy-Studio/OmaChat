pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

Dialog {
    id: dialog
    objectName: "sidebarSettingsDialog"
    title: qsTr("Sidebar — On this device")
    property string account: ""
    property var hidden: []
    width: Math.min(Metrics.px(500), (parent ? parent.width : 800) - Metrics.px(32))
    height: Math.min(Metrics.px(480), (parent ? parent.height : 600) - Metrics.px(32))
    function refresh() {
        if (visible && account !== App.accountId) { close(); return }
        hidden = App.hiddenChannels(search.text)
    }
    function reveal(item) {
        if (!item.activeFocus) return
        const flick = scroll.contentItem as Flickable
        if (!flick) return
        const point = item.mapToItem(flick.contentItem, 0, 0)
        const maximum = Math.max(0, flick.contentHeight - flick.height)
        if (point.y < flick.contentY) flick.contentY = Math.max(0, point.y)
        else if (point.y + item.height > flick.contentY + flick.height)
            flick.contentY = Math.min(maximum, point.y + item.height - flick.height)
    }
    onOpened: {
        account = App.accountId
        search.text = ""
        refresh()
        search.input.forceActiveFocus()
    }
    Connections {
        target: App
        function onSidebarChanged() { dialog.refresh() }
        function onChannelDataChanged(id) { dialog.refresh() }
        function onStatusChanged() { dialog.refresh() }
    }
    contentItem: ColumnLayout {
        spacing: Metrics.px(10)
        Text {
            Layout.fillWidth: true
            text: dialog.title
            color: Theme.text
            font.pixelSize: Metrics.px(16)
            font.bold: true
            wrapMode: Text.Wrap
        }
        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: parent.width
                spacing: Metrics.px(10)
                Check {
                    objectName: "sidebarCompactToggle"
                    text: qsTr("Compact channel rows")
                    checked: App.sidebarCompact
                    enabled: !App.sidebarSaving
                    onToggled: App.setSidebarPreferences({compact: checked})
                    onActiveFocusChanged: dialog.reveal(this)
                }
                Check {
                    objectName: "sidebarParticipantsToggle"
                    text: qsTr("Show voice participants")
                    checked: App.showVoiceParticipants
                    enabled: !App.sidebarSaving
                    onToggled: App.setSidebarPreferences({show_voice_participants: checked})
                    onActiveFocusChanged: dialog.reveal(this)
                }
                Text {
                    text: qsTr("Hidden channels")
                    color: Theme.text
                    font.pixelSize: Metrics.px(14)
                    font.bold: true
                    Accessible.role: Accessible.Heading
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Hiding keeps notifications enabled. Search can still open a hidden channel. The current channel and active voice call stay visible.")
                    wrapMode: Text.Wrap
                    color: Theme.textMuted
                    font.pixelSize: Metrics.px(12)
                }
                Field {
                    id: search
                    objectName: "hiddenChannelSearch"
                    Layout.fillWidth: true
                    label: qsTr("Find hidden channels")
                    placeholder: qsTr("Channel or server name")
                    onTextChanged: dialog.refresh()
                    Connections {
                        target: search.input
                        function onActiveFocusChanged() { dialog.reveal(search.input) }
                    }
                }
                Text {
                    visible: dialog.hidden.length === 0
                    Layout.fillWidth: true
                    text: search.text.length ? qsTr("No hidden channels match this search.")
                                            : qsTr("No hidden channels. Use a channel menu to hide one.")
                    wrapMode: Text.Wrap
                    color: Theme.textMuted
                    font.pixelSize: Metrics.px(12)
                }
                Repeater {
                    model: dialog.hidden
                    delegate: RowLayout {
                        id: hiddenRow
                        required property var modelData
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: hiddenRow.modelData.name + (hiddenRow.modelData.server ? " · " + hiddenRow.modelData.server : "")
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: Theme.text
                            font.pixelSize: Metrics.px(13)
                        }
                        FlatButton {
                            text: qsTr("Open")
                            Accessible.name: qsTr("Open %1").arg(hiddenRow.modelData.name)
                            onClicked: { App.selectChannel(hiddenRow.modelData.id); dialog.close() }
                            onActiveFocusChanged: dialog.reveal(this)
                        }
                        FlatButton {
                            objectName: "unhide_" + hiddenRow.modelData.id
                            text: qsTr("Unhide")
                            Accessible.name: qsTr("Unhide %1").arg(hiddenRow.modelData.name)
                            enabled: !App.sidebarSaving
                            onClicked: { search.input.forceActiveFocus(); App.setSidebarPreferences({channel: hiddenRow.modelData.id, hidden: false}) }
                            onActiveFocusChanged: dialog.reveal(this)
                        }
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            visible: App.sidebarSaving || App.sidebarError.length > 0
            text: App.sidebarSaving ? qsTr("Saving…") : App.sidebarError
            color: App.sidebarError.length ? Theme.danger : Theme.textMuted
            wrapMode: Text.Wrap
            font.pixelSize: Metrics.px(12)
            Accessible.name: text
        }
        FlatButton {
            objectName: "sidebarSettingsClose"
            Layout.alignment: Qt.AlignRight
            text: qsTr("Close")
            onClicked: dialog.close()
        }
    }
}
