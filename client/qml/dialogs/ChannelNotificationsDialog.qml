pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

Dialog {
    id: dialog
    objectName: "channelNotificationsDialog"
    title: qsTr("Notifications")
    property string channelId: ""
    property string accountId: ""
    property var preferences: ({})
    property bool saving: false
    property string saveError: ""
    readonly property var levels: ["default", "all", "mentions", "none"]
    readonly property var durations: ["1h", "8h", "24h", "forever"]
    protectClose: saving
    width: Math.min(Metrics.px(440), (parent ? parent.width : 800) - Metrics.px(40))
    height: Math.min(Metrics.px(390), (parent ? parent.height : 600) - Metrics.px(40))

    function reveal(item) {
        if (!item.activeFocus) return
        const flick = formScroll.contentItem as Flickable
        if (!flick) return
        const point = item.mapToItem(flick.contentItem, 0, 0)
        const maximum = Math.max(0, flick.contentHeight - flick.height)
        if (point.y < flick.contentY) flick.contentY = Math.max(0, point.y)
        else if (point.y + item.height > flick.contentY + flick.height)
            flick.contentY = Math.min(maximum, point.y + item.height - flick.height)
    }
    function refresh() {
        if (!channelId.length) return
        if (App.accountId !== accountId || !App.ready || !App.channelDetails(channelId).id) {
            close()
            return
        }
        preferences = App.channelNotificationPreferences(channelId)
        levelPicker.currentIndex = Math.max(0, levels.indexOf(preferences.notification_level || "default"))
    }
    function openFor(id) {
        if (visible) return
        accountId = App.accountId
        channelId = id
        saving = false
        saveError = ""
        durationPicker.currentIndex = 0
        refresh()
        if (App.ready && App.accountId === accountId && App.channelDetails(id).id) open()
    }
    onOpened: levelPicker.forceActiveFocus()
    Connections {
        target: App
        function onChannelPreferencesChanged() { dialog.refresh() }
        function onStatusChanged() { dialog.refresh() }
        function onChannelDataChanged(id) { if (id === dialog.channelId) dialog.refresh() }
        function onChannelPreferencesWriteFinished(account, channel, ok, error) {
            if (!dialog.visible || account !== dialog.accountId || channel !== dialog.channelId) return
            dialog.saving = false
            dialog.saveError = ok ? "" : error
            dialog.refresh()
        }
    }
    component Picker: ComboBox {
        onActiveFocusChanged: dialog.reveal(this)
        Layout.fillWidth: true
        implicitHeight: Metrics.px(34)
        font.pixelSize: Metrics.px(13)
        palette.button: Theme.surfaceAlt
        palette.buttonText: Theme.text
        palette.window: Theme.raised
        palette.text: Theme.text
        palette.highlight: Theme.selection
        palette.highlightedText: Theme.text
    }
    contentItem: ColumnLayout {
        spacing: Metrics.px(10)
        Text {
            Layout.fillWidth: true
            text: qsTr("Notifications — On this device")
            wrapMode: Text.Wrap
            color: Theme.text
            font.pixelSize: Metrics.px(16)
            font.bold: true
        }
        ScrollView {
            id: formScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: Metrics.px(10)
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Notification level")
                    color: Theme.textMuted
                    font.pixelSize: Metrics.px(12)
                }
                Picker {
                    id: levelPicker
                    objectName: "notificationLevelPicker"
                    Accessible.name: qsTr("Notification level on this device")
                    model: [qsTr("Default"), qsTr("All messages"), qsTr("Mentions only"), qsTr("None")]
                    enabled: !dialog.saving
                    onActivated: {
                        dialog.saving = true
                        dialog.saveError = ""
                        App.setChannelNotificationLevel(dialog.channelId, dialog.levels[currentIndex])
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Global notification switches and Do not disturb still apply.")
                    wrapMode: Text.Wrap
                    color: Theme.textMuted
                    font.pixelSize: Metrics.px(11)
                }
                Text {
                    objectName: "notificationMuteState"
                    Layout.fillWidth: true
                    text: !dialog.preferences.muted ? qsTr("Not muted")
                        : dialog.preferences.mute_until_ms === "0" ? qsTr("Muted until changed")
                        : qsTr("Muted until %1").arg(new Date(Number(dialog.preferences.mute_until_ms)).toLocaleString(Qt.locale(), Locale.ShortFormat))
                    wrapMode: Text.Wrap
                    color: Theme.text
                    font.pixelSize: Metrics.px(12)
                }
                Picker {
                    id: durationPicker
                    objectName: "notificationMuteDuration"
                    Accessible.name: qsTr("Mute duration")
                    model: [qsTr("1 hour"), qsTr("8 hours"), qsTr("24 hours"), qsTr("Until changed")]
                    enabled: !dialog.saving
                }
                RowLayout {
                    Layout.fillWidth: true
                    FlatButton {
                        onActiveFocusChanged: dialog.reveal(this)
                        objectName: "notificationMuteButton"
                        text: qsTr("Mute")
                        enabled: !dialog.saving
                        onClicked: {
                            dialog.saving = true
                            dialog.saveError = ""
                            App.muteChannelFor(dialog.channelId, dialog.durations[durationPicker.currentIndex])
                        }
                    }
                    FlatButton {
                        onActiveFocusChanged: dialog.reveal(this)
                        objectName: "notificationUnmuteButton"
                        text: qsTr("Unmute")
                        enabled: !!dialog.preferences.muted && !dialog.saving
                        onClicked: {
                            dialog.saving = true
                            dialog.saveError = ""
                            App.muteChannelFor(dialog.channelId, "")
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: dialog.saving || dialog.saveError.length > 0
                    text: dialog.saving ? qsTr("Saving…") : dialog.saveError
                    wrapMode: Text.Wrap
                    color: dialog.saveError.length ? Theme.danger : Theme.textMuted
                    font.pixelSize: Metrics.px(12)
                }
            }
        }
        FlatButton {
            objectName: "notificationCloseButton"
            Layout.alignment: Qt.AlignRight
            text: qsTr("Close")
            enabled: !dialog.saving
            onClicked: dialog.close()
        }
    }
}
