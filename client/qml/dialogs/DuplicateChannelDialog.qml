import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

Dialog {
    id: dialog
    objectName: "duplicateChannelDialog"
    property string channelId: ""
    property string accountId: ""
    property string serverId: ""
    property var details: ({})
    property bool saving: false
    property bool uncertain: false
    property string error: ""
    title: details.type === "category" ? qsTr("Duplicate category") : qsTr("Duplicate channel")
    width: Math.min(Metrics.px(480), (parent ? parent.width : 800) - Metrics.px(40))
    height: Math.min(Metrics.px(440), (parent ? parent.height : 600) - Metrics.px(40))
    protectClose: saving
    function openFor(id) {
        if (visible) return
        channelId = id
        accountId = App.accountId
        serverId = App.selectedServerId
        details = App.channelDetails(id)
        if (!details.id || details.server_id !== serverId
            || (details.type !== "text" && details.type !== "voice" && details.type !== "category")) return
        saving = false
        uncertain = false
        error = ""
        nameField.text = (details.name || "") + "-copy"
        open()
    }
    function submit() {
        if (!saveButton.enabled) return
        saving = true
        error = ""
        App.duplicateChannel(channelId, nameField.text)
    }
    function checkContext() {
        if (!visible) return
        const current = App.channelDetails(channelId)
        if (App.accountId !== accountId || App.selectedServerId !== serverId || !current.id) {
            saving = false
            close()
        } else if (saving && App.state !== "connected") {
            saving = false
            uncertain = true
            error = qsTr("Connection lost. The copy may have been created. Check the channel list before starting another copy.")
        }
        details = current
    }
    onOpened: nameField.input.forceActiveFocus()
    Connections {
        target: App
        function onStatusChanged() { dialog.checkContext() }
        function onSelectionChanged() { dialog.checkContext() }
        function onChannelDataChanged(id) { dialog.checkContext() }
        function onArtworkGenerationChanged() {
            if (dialog.visible && dialog.saving) {
                dialog.saving = false
                dialog.uncertain = true
                dialog.error = qsTr("Access context changed. Check the channel list before starting another copy.")
            }
            dialog.checkContext()
        }
        function onAdministrationFinished(operation, id, failure, saved) {
            if (!dialog.visible || !dialog.saving || operation !== "channel.duplicate" || id !== dialog.channelId) return
            dialog.saving = false
            dialog.error = failure
            if (failure && saved && saved.uncertain) dialog.uncertain = true
            if (!failure) dialog.close()
        }
    }
    contentItem: ColumnLayout {
        spacing: Metrics.px(14)
        Text {
            Layout.fillWidth: true
            text: dialog.title
            textFormat: Text.PlainText
            color: Theme.text
            font.pixelSize: Metrics.px(16)
            font.bold: true
        }
        ScrollView {
            id: formScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            clip: true
            ColumnLayout {
                width: formScroll.availableWidth
                spacing: Metrics.px(14)
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Copy %1 in its current category. Details, artwork and channel permission overrides are copied. Category permissions continue to apply.").arg(dialog.details.name || "")
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    color: Theme.textMuted
                    font.pixelSize: Metrics.px(13)
                }
                Text {
                    Layout.fillWidth: true
                    text: dialog.details.type === "category" ? qsTr("Messages, members and child channels are excluded.") : qsTr("Messages and members are excluded.")
                    wrapMode: Text.Wrap
                    color: Theme.textMuted
                    font.pixelSize: Metrics.px(13)
                }
                Field {
                    id: nameField
                    objectName: "duplicateChannelName"
                    Layout.fillWidth: true
                    label: qsTr("New name")
                    enabled: !dialog.saving && !dialog.uncertain
                    input.maximumLength: 64
                    input.onAccepted: dialog.submit()
                }
                Text {
                    Layout.fillWidth: true
                    text: dialog.error
                    textFormat: Text.PlainText
                    visible: text.length > 0
                    wrapMode: Text.Wrap
                    color: Theme.danger
                    font.pixelSize: Metrics.px(12)
                }
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            FlatButton { text: qsTr("Close"); enabled: !dialog.saving; onClicked: dialog.close() }
            FlatButton {
                id: saveButton
                objectName: "duplicateChannelSubmit"
                text: dialog.saving ? qsTr("Duplicating…") : qsTr("Duplicate")
                primary: true
                enabled: !dialog.saving && !dialog.uncertain && nameField.text.trim().length > 0
                    && (dialog.details.permissions || []).indexOf("MANAGE_CHANNEL") >= 0
                    && App.state === "connected" && App.capabilities.indexOf("channel.duplicate.v1") >= 0
                onClicked: dialog.submit()
            }
        }
    }
}
