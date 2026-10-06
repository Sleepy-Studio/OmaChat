import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

Dialog {
    id: dialog
    objectName: "channelPlacementDialog"
    property string channelId: ""
    property string serverId: ""
    property string accountId: ""
    property var details: ({})
    property bool saving: false
    property bool failed: false
    property string status: ""
    property var categories: []
    property var positions: []
    title: details.type === "category" ? qsTr("Place category") : qsTr("Place channel")
    width: Math.min(Metrics.px(500), (parent ? parent.width : 800) - Metrics.px(40))
    height: Math.min(Metrics.px(440), (parent ? parent.height : 600) - Metrics.px(40))
    protectClose: saving
    Shortcut {
        sequence: "Escape"
        enabled: dialog.visible && dialog.activeFocus && !dialog.saving
            && !categoryPicker.popup.visible && !positionPicker.popup.visible
        onActivated: dialog.close()
    }
    onOpened: categoryPicker.visible ? categoryPicker.forceActiveFocus() : positionPicker.forceActiveFocus()

    function ensureControlVisible(item: Item) {
        const view = formScroll.contentItem as Flickable
        if (view) view.contentY = Math.max(0, item.y + item.height - formScroll.availableHeight)
    }

    function refreshPositions(initial) {
        const parentId = categories[categoryPicker.currentIndex]?.id || "0"
        const siblings = App.channelPlacementSiblings(channelId, parentId)
        let choices = []
        let chosen = siblings.length
        for (let i = 0; i < siblings.length; ++i) {
            choices.push({id: siblings[i].id, name: qsTr("Before %1").arg(siblings[i].name)})
            if (initial && chosen === siblings.length && siblings[i].position >= (details.position || 0))
                chosen = i
        }
        choices.push({id: "", name: qsTr("At the end")})
        positions = choices
        positionPicker.currentIndex = initial ? chosen : siblings.length
    }

    function openFor(id) {
        if (visible) return
        channelId = id
        serverId = App.selectedServerId
        accountId = App.accountId
        details = App.channelDetails(id)
        saving = false
        failed = false
        status = ""
        categories = details.type === "category" ? [{id: "0", name: qsTr("Top level")}] : App.channelCategories()
        let chosen = 0
        for (let i = 0; i < categories.length; ++i)
            if (categories[i].id === details.parent_id) chosen = i
        categoryPicker.currentIndex = chosen
        refreshPositions(true)
        open()
    }

    function closeForContextChange() {
        if (visible && (App.selectedServerId !== serverId || App.accountId !== accountId)) {
            saving = false
            close()
        }
    }

    Connections {
        target: App
        function onArtworkGenerationChanged() {
            if (dialog.visible) {
                dialog.saving = false
                dialog.close()
            }
        }
        function onSelectionChanged() { dialog.closeForContextChange() }
        function onStatusChanged() { dialog.closeForContextChange() }
        function onAdministrationFinished(operation, id, error, saved) {
            if (!dialog.visible || !dialog.saving || operation !== "channel.move" || id !== dialog.channelId) return
            dialog.saving = false
            dialog.failed = error.length > 0
            dialog.status = error || qsTr("Placement saved.")
            if (!error) {
                dialog.details = saved
                dialog.refreshPositions(true)
            }
            Qt.callLater(() => { if (dialog.visible && !dialog.saving) saveButton.forceActiveFocus() })
        }
    }

    contentItem: ColumnLayout {
        spacing: Metrics.px(12)
        Text {
            Layout.fillWidth: true
            text: qsTr("%1: %2").arg(dialog.title).arg(dialog.details.name || "")
            textFormat: Text.PlainText
            elide: Text.ElideRight
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
                spacing: Metrics.px(12)
                Text {
                    Layout.fillWidth: true
                    text: dialog.details.type === "category" ? qsTr("Categories are ordered among categories at top level.")
                        : qsTr("Choose a category and a position among its channels. Moving changes inherited category permissions immediately; channel overrides stay in place.")
                    wrapMode: Text.Wrap
                    color: Theme.textMuted
                    font.pixelSize: Metrics.px(12)
                }
                ComboBox {
                    id: categoryPicker
                    objectName: "placementCategory"
                    Layout.fillWidth: true
                    visible: dialog.details.type !== "category"
                    enabled: !dialog.saving && App.ready && App.canManageChannels
                    model: dialog.categories
                    textRole: "name"
                    valueRole: "id"
                    palette.button: Theme.surfaceAlt
                    palette.buttonText: Theme.text
                    palette.window: Theme.raised
                    palette.text: Theme.text
                    palette.highlight: Theme.selection
                    palette.highlightedText: Theme.text
                    font.pixelSize: Metrics.px(13)
                    onActiveFocusChanged: {
                        if (activeFocus) dialog.ensureControlVisible(categoryPicker)
                    }
                    Accessible.name: qsTr("Destination category")
                    onActivated: { dialog.status = ""; dialog.refreshPositions(false) }
                }
                ComboBox {
                    id: positionPicker
                    objectName: "placementPosition"
                    Layout.fillWidth: true
                    enabled: !dialog.saving && App.ready && App.canManageChannels
                    model: dialog.positions
                    textRole: "name"
                    valueRole: "id"
                    palette.button: Theme.surfaceAlt
                    palette.buttonText: Theme.text
                    palette.window: Theme.raised
                    palette.text: Theme.text
                    palette.highlight: Theme.selection
                    palette.highlightedText: Theme.text
                    font.pixelSize: Metrics.px(13)
                    onActiveFocusChanged: {
                        if (activeFocus) dialog.ensureControlVisible(positionPicker)
                    }
                    Accessible.name: qsTr("Sibling position")
                    onActivated: dialog.status = ""
                }

            }
        }
        Text {
            objectName: "placementStatus"
            Layout.fillWidth: true
            visible: text.length > 0
            text: !App.ready ? qsTr("Reconnect before saving placement.")
                : !App.canManageChannels ? qsTr("Channel management permission is required.") : dialog.status
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: dialog.failed || !App.ready || !App.canManageChannels ? Theme.danger : Theme.textMuted
            font.pixelSize: Metrics.px(12)
            Accessible.name: text
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            FlatButton {
                text: qsTr("Close")
                enabled: !dialog.saving
                onClicked: dialog.close()
            }
            FlatButton {
                id: saveButton
                objectName: "placementSave"
                text: dialog.saving ? qsTr("Saving…") : qsTr("Save placement")
                primary: true
                enabled: !dialog.saving && App.ready && App.canManageChannels
                    && App.capabilities.indexOf("channel.placement.v1") >= 0
                    && positionPicker.currentIndex >= 0
                onClicked: {
                    dialog.saving = true
                    dialog.failed = false
                    dialog.status = qsTr("Saving placement…")
                    App.placeChannel(dialog.channelId, categoryPicker.currentValue || "0", positionPicker.currentValue || "")
                }
            }
        }
    }
}
