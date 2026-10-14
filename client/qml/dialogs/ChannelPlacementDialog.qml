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
    property bool needsReload: false
    property var baseline: ({})
    property var originalPlacement: ({})
    readonly property bool checkedListSupported: App.capabilities.indexOf("channel.placement.list.cas.v1") >= 0
    readonly property bool checkedPlacementSupported: App.capabilities.indexOf("channel.placement.cas.v1") >= 0
    readonly property bool canManage: App.state === "connected" && App.canManageChannels
        && App.accountId === accountId && App.selectedServerId === serverId
        && details.id === channelId && details.server_id === serverId
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

    // This detects changes in the visible model. Atomic server-side placement
    // comparison remains necessary to cover hidden siblings and in-flight races.
    function placementSnapshot() {
        const current = App.channelDetails(channelId)
        const parentId = categoryPicker.currentValue || "0"
        const siblings = App.channelPlacementSiblings(channelId, parentId)
        return {
            token: current.placement_token || "",
            source: JSON.stringify([current.id || "", current.parent_id || "0", current.position || 0, current.type || ""]),
            destination: parentId,
            siblings: JSON.stringify(siblings.map(sibling => [sibling.id, sibling.position])),
            categories: JSON.stringify(App.channelCategories().map(category => category.id).sort())
        }
    }

    function checkPlacement() {
        if (!visible) return
        details = App.channelDetails(channelId)
        if (saving || needsReload) return
        const current = placementSnapshot()
        if (current.token !== baseline.token || current.source !== baseline.source || current.destination !== baseline.destination
                || current.siblings !== baseline.siblings || current.categories !== baseline.categories) {
            needsReload = true
            failed = true
            status = qsTr("Available placement changed. Your choices are retained. Reload positions and review them before saving.")
        }
    }

    function reloadPlacement() {
        if (!canManage || saving) return
        if (checkedListSupported) {
            saving = true
            status = qsTr("Loading current positions…")
            App.reloadChannelPlacement(channelId)
            return
        }
        applyReloadedPlacement()
    }

    function applyReloadedPlacement() {
        const parentId = categoryPicker.currentValue || "0"
        const beforeId = positionPicker.currentValue || ""
        details = App.channelDetails(channelId)
        categories = details.type === "category" ? [{id: "0", name: qsTr("Top level")}] : App.channelCategories()
        let chosen = 0
        let parentRetained = false
        for (let i = 0; i < categories.length; ++i) {
            if (categories[i].id === parentId) { chosen = i; parentRetained = true; break }
        }
        categoryPicker.currentIndex = chosen
        refreshPositions(false)
        let positionRetained = beforeId === ""
        for (let i = 0; i < positions.length; ++i) {
            if (positions[i].id === beforeId) { positionPicker.currentIndex = i; positionRetained = true; break }
        }
        baseline = placementSnapshot()
        originalPlacement = {parent_id: details.parent_id || "0", position: details.position || 0, placement_token: details.placement_token || ""}
        needsReload = false
        failed = false
        status = parentRetained && positionRetained
            ? qsTr("Current positions loaded. Your choices are retained. Review them before saving.")
            : qsTr("A chosen destination or sibling is no longer available. Review the updated category and position before saving.")
        Qt.callLater(() => { if (dialog.visible) categoryPicker.visible ? categoryPicker.forceActiveFocus() : positionPicker.forceActiveFocus() })
    }

    function openFor(id) {
        if (visible) return
        channelId = id
        serverId = App.selectedServerId
        accountId = App.accountId
        details = App.channelDetails(id)
        if (!details.id || details.server_id !== serverId) return
        saving = false
        failed = false
        needsReload = false
        status = ""
        categories = details.type === "category" ? [{id: "0", name: qsTr("Top level")}] : App.channelCategories()
        let chosen = 0
        for (let i = 0; i < categories.length; ++i)
            if (categories[i].id === details.parent_id) chosen = i
        categoryPicker.currentIndex = chosen
        refreshPositions(true)
        baseline = placementSnapshot()
        originalPlacement = {parent_id: details.parent_id || "0", position: details.position || 0, placement_token: details.placement_token || ""}
        open()
    }

    function closeForContextChange() {
        if (visible && (App.selectedServerId !== serverId || App.accountId !== accountId)) {
            saving = false
            close()
        }
    }

    Connections {
        target: App.channels
        function onModelReset() { dialog.checkPlacement() }
    }

    Connections {
        target: App
        function onChannelDataChanged(id) { dialog.checkPlacement() }
        function onArtworkGenerationChanged() {
            if (dialog.visible) {
                dialog.saving = false
                dialog.close()
            }
        }
        function onSelectionChanged() { dialog.closeForContextChange() }
        function onStatusChanged() { dialog.closeForContextChange() }
        function onAdministrationFinished(operation, id, error, saved) {
            if (!dialog.visible || !dialog.saving || id !== dialog.channelId) return
            if (operation === "channel.placement.reload") {
                dialog.saving = false
                if (error) {
                    dialog.failed = true
                    dialog.needsReload = true
                    dialog.status = error
                } else {
                    dialog.details = saved
                    dialog.applyReloadedPlacement()
                }
                return
            }
            if (operation !== "channel.move") return
            dialog.saving = false
            dialog.failed = error.length > 0
            if (error && saved && saved.conflict) dialog.needsReload = true
            dialog.status = error || qsTr("Placement saved.")
            if (!error) {
                dialog.details = saved
                dialog.refreshPositions(true)
                dialog.baseline = dialog.placementSnapshot()
                dialog.needsReload = false
                dialog.originalPlacement = {parent_id: saved.parent_id || "0", position: saved.position || 0, placement_token: saved.placement_token || ""}
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
                    enabled: !dialog.saving && dialog.canManage
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
                    onActivated: {
                        dialog.refreshPositions(false)
                        // Retain the original source baseline while choosing another destination.
                        if (!dialog.needsReload) {
                            const next = dialog.placementSnapshot()
                            next.source = dialog.baseline.source
                            next.token = dialog.baseline.token
                            dialog.baseline = next
                            dialog.status = ""
                            dialog.checkPlacement()
                        }
                    }
                }
                ComboBox {
                    id: positionPicker
                    objectName: "placementPosition"
                    Layout.fillWidth: true
                    enabled: !dialog.saving && dialog.canManage
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
                    onActivated: { if (!dialog.needsReload) dialog.status = "" }
                }

            }
        }
        Text {
            objectName: "placementStatus"
            Layout.fillWidth: true
            visible: text.length > 0
            text: App.state !== "connected" ? qsTr("Reconnect before saving placement.")
                : !App.canManageChannels ? qsTr("Channel management permission is required.")
                : !dialog.canManage ? qsTr("This channel is no longer available. Close placement and choose another channel.")
                : !dialog.checkedPlacementSupported ? qsTr("This server needs an update before checked placement can be saved.") : dialog.status
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: dialog.failed || !dialog.canManage ? Theme.danger : Theme.textMuted
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
                objectName: "placementReload"
                text: qsTr("Reload positions")
                visible: dialog.needsReload
                enabled: !dialog.saving && dialog.canManage
                onClicked: dialog.reloadPlacement()
            }
            FlatButton {
                id: saveButton
                objectName: "placementSave"
                text: dialog.saving ? qsTr("Saving…") : qsTr("Save placement")
                primary: true
                enabled: !dialog.saving && !dialog.needsReload && dialog.canManage
                    && App.capabilities.indexOf("channel.placement.v1") >= 0 && dialog.checkedPlacementSupported
                    && positionPicker.currentIndex >= 0
                onClicked: {
                    dialog.checkPlacement()
                    if (dialog.needsReload || !dialog.canManage) return
                    dialog.saving = true
                    dialog.failed = false
                    dialog.status = qsTr("Saving placement…")
                    App.placeChannel(dialog.channelId, categoryPicker.currentValue || "0", positionPicker.currentValue || "", dialog.originalPlacement)
                }
            }
        }
    }
}
