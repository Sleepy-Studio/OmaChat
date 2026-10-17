pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import OmaChat

Dialog {
    id: dialog
    objectName: "channelSettingsDialog"
    property int section: 0
    property var review: ({ available: false, rows: [] })
    readonly property bool canManage: App.state === "connected" && App.canManageChannels
        && details.id === channelId && details.server_id === serverId && App.selectedServerId === serverId
        && (details.permissions || []).indexOf("MANAGE_CHANNEL") >= 0
    property bool artworkSaving: false
    property bool artworkReloading: false
    property bool artworkNeedsReload: false
    property string artworkStatus: ""
    readonly property bool canChangeArtwork: canManage && !artworkSaving && !artworkReloading && !artworkNeedsReload && App.capabilities.indexOf("channel.identity.v1") >= 0
        && App.capabilities.indexOf("channel.artwork.cas.v1") >= 0
    signal openPermissionsRequested(string id, string name)

    function refreshReview() { review = App.channelPermissionReview(channelId) }
    function selectSection(index) {
        section = Math.max(0, Math.min(2, index))
        if (section === 2) refreshReview()
        if (formScroll.contentItem) formScroll.contentItem.contentY = 0
        if (section === 2) Qt.callLater(() => formScroll.contentItem.forceActiveFocus())
    }
    function revealControl(item) {
        if (!item.activeFocus) return
        const flick = formScroll.contentItem as Flickable
        if (!flick) return
        const point = item.mapToItem(flick.contentItem, 0, 0)
        const maximum = Math.max(0, flick.contentHeight - flick.height)
        if (point.y < flick.contentY) flick.contentY = Math.max(0, point.y)
        else if (point.y + item.height > flick.contentY + flick.height)
            flick.contentY = Math.min(maximum, point.y + item.height - flick.height)
    }
    function syncCategory() {
        const cats = App.channelCategories()
        let chosen = 0
        for (let i = 0; i < cats.length; ++i)
            if (cats[i].id === details.parent_id) chosen = i
        categoryPicker.currentIndex = chosen
    }
    property var original: ({})
    property var submitted: ({})
    property bool saving: false
    property bool reloading: false
    property bool needsReload: false
    property bool latestLoaded: false
    readonly property bool checkedDetailsSupported: App.capabilities.indexOf("channel.details.cas.v1") >= 0
    property string saveStatus: ""
    property bool saveFailed: false
    readonly property bool dirty: nameField.text !== (original.name || "")
        || topicField.text !== (original.topic || "")
        || descriptionField.text !== (original.description || "")
    protectClose: dirty || saving || reloading || artworkSaving || artworkReloading
    onCloseRequested: { if (!saving && !reloading && !artworkSaving && !artworkReloading) discardConfirm.open() }
    height: Math.min(Metrics.px(740), (parent ? parent.height : 800) - Metrics.px(40))
    property var artworkEpoch: 0
    property string artworkScope: ""
    property string accountId
    property string serverId
    property string channelId
    property var details: ({})
    title: qsTr("Channel settings")
    width: Math.min(Metrics.px(560), (parent ? parent.width : 800) - Metrics.px(40))

    function openFor(id) {
        if (visible) return
        section = 0
        artworkSaving = false
        artworkReloading = false
        artworkNeedsReload = false
        artworkStatus = ""
        saving = false
        reloading = false
        needsReload = false
        latestLoaded = false
        saveStatus = ""
        saveFailed = false
        artworkEpoch = App.artworkGeneration
        artworkScope = App.artworkScopeFingerprint
        accountId = App.accountId
        serverId = App.selectedServerId
        channelId = id
        details = App.channelDetails(id)
        if (!details.id || details.server_id !== serverId) return
        original = details
        nameField.text = details.name || ""
        topicField.text = details.topic || ""
        descriptionField.text = details.description || ""
        syncCategory()
        refreshReview()
        if (details.icon_attachment_id && details.icon_attachment_id !== "0")
            App.requestPreview(details.icon_attachment_id, "channel-icon.png", 0)
        if (details.banner_attachment_id && details.banner_attachment_id !== "0")
            App.requestPreview(details.banner_attachment_id, "channel-banner.png", 0)
        open()
    }

    function removeArtwork(kind) {
        if (!canChangeArtwork || saving || reloading) return
        artworkSaving = true
        artworkStatus = qsTr("Removing artwork…")
        App.setChannelArtwork(channelId, kind, "", artworkCrop.artworkBaseline(details))
    }
    function reloadArtwork() {
        if (!canManage || saving || reloading || artworkSaving || artworkReloading) return
        artworkReloading = true
        artworkStatus = qsTr("Reloading artwork…")
        App.reloadChannelDetails(channelId)
    }
    function cancelForContextChange() {
        artworkSaving = false
        artworkReloading = false
        saving = false
        reloading = false
        discardConfirm.close()
        close()
    }

    Connections {
        target: App.channels
        function onModelReset() {
            if (dialog.visible && !App.channelDetails(dialog.channelId).id) dialog.cancelForContextChange()
        }
    }

    Connections {
        target: App
        function onArtworkGenerationChanged() {
            if (!dialog.visible) return
            const current = App.channelDetails(dialog.channelId)
            const sameScopeEpoch = App.artworkGeneration !== dialog.artworkEpoch && dialog.artworkScope.length > 0
                && App.artworkScopeFingerprint === dialog.artworkScope
                && App.accountId === dialog.accountId && App.selectedServerId === dialog.serverId
                && App.state === "connected" && App.canManageChannels
                && current.id === dialog.channelId && current.server_id === dialog.serverId
                && (current.permissions || []).indexOf("MANAGE_CHANNEL") >= 0
            if (!sameScopeEpoch) { dialog.cancelForContextChange(); return }
            dialog.artworkEpoch = App.artworkGeneration
            dialog.details = current
            if (dialog.saving || dialog.reloading) {
                dialog.saving = false
                dialog.reloading = false
                dialog.needsReload = true
                dialog.saveFailed = true
                dialog.latestLoaded = false
                dialog.saveStatus = qsTr("Artwork availability changed during the earlier details request. It may have completed. Your text edits are retained. Reload details and review them before saving again.")
            }
            dialog.artworkReloading = false
            dialog.artworkNeedsReload = true
            // A checked removal settles through its account/scope-bound reply.
            dialog.artworkStatus = dialog.artworkSaving
                ? qsTr("Artwork availability changed. Waiting for the checked removal result; your text edits are retained.")
                : qsTr("Artwork availability changed. Your text edits are retained. Reload artwork before changing it again.")
        }
        function onStatusChanged() {
            dialog.refreshReview()
            if (dialog.visible && App.accountId !== dialog.accountId) dialog.cancelForContextChange()
        }
        function onSelectionChanged() {
            dialog.refreshReview()
            if (dialog.visible && App.selectedServerId !== dialog.serverId) dialog.cancelForContextChange()
        }
        function onChannelDataChanged(id) {
            if (id === dialog.channelId || id === dialog.review.categoryId) dialog.refreshReview()
            if (id !== dialog.channelId)
                return
            const keepInput = dialog.dirty || dialog.saving || dialog.reloading || dialog.needsReload
            dialog.details = App.channelDetails(id)
            dialog.syncCategory()
            if (!keepInput) {
                dialog.original = dialog.details
                nameField.text = dialog.details.name || ""
                topicField.text = dialog.details.topic || ""
                descriptionField.text = dialog.details.description || ""
            }
            if (dialog.details.icon_attachment_id && dialog.details.icon_attachment_id !== "0")
                App.requestPreview(dialog.details.icon_attachment_id, "channel-icon.png", 0)
            if (dialog.details.banner_attachment_id && dialog.details.banner_attachment_id !== "0")
                App.requestPreview(dialog.details.banner_attachment_id, "channel-banner.png", 0)
        }
        function onAdministrationFinished(operation, id, error, saved) {
            if (!dialog.visible) return
            if (operation === "channel.artwork.set" && id === dialog.channelId && dialog.artworkSaving) {
                dialog.artworkSaving = false
                dialog.artworkNeedsReload = !!(error && saved && saved.conflict)
                dialog.artworkStatus = error || qsTr("Artwork removed.")
                return
            }
            if (operation === "channel.details.reload" && id === dialog.channelId && dialog.artworkReloading) {
                dialog.artworkReloading = false
                dialog.artworkStatus = error || qsTr("Current artwork loaded. Review it before choosing Remove again.")
                if (!error) {
                    dialog.details = saved
                    dialog.artworkNeedsReload = false
                }
                return
            }
            if (operation === "channel.details.reload" && id === dialog.channelId && dialog.reloading) {
                dialog.reloading = false
                dialog.saveFailed = error.length > 0
                if (error) {
                    dialog.saveStatus = error
                } else {
                    dialog.details = saved
                    dialog.syncCategory()
                    dialog.original = saved
                    dialog.needsReload = false
                    dialog.latestLoaded = true
                    dialog.saveStatus = qsTr("Latest details loaded. Your edits are retained. Review them against the latest values in the form, then save deliberately.")
                }
                reloadDetails.forceActiveFocus()
                return
            }
            if (operation !== "channel.update" || id !== dialog.channelId || !dialog.saving) return
            dialog.saving = false
            dialog.saveFailed = error.length > 0
            if (error && saved && saved.conflict) dialog.needsReload = true
            dialog.saveStatus = error || qsTr("Channel details saved.")
            if (!error) {
                if (nameField.text === dialog.submitted.name) nameField.text = saved.name || ""
                if (topicField.text === dialog.submitted.topic) topicField.text = saved.topic || ""
                if (descriptionField.text === dialog.submitted.description) descriptionField.text = saved.description || ""
                dialog.original = saved
            }
            if (error) {
                if (dialog.needsReload) reloadDetails.forceActiveFocus()
                else nameField.input.forceActiveFocus()
            } else closeDetails.forceActiveFocus()
        }
    }

    ConfirmDialog {
        id: discardConfirm
        onClosed: { if (dialog.visible) dialog.contentItem.forceActiveFocus() }
        objectName: "discardChannel"
        title: qsTr("Discard channel edits?")
        message: qsTr("Your unsaved name, topic and description will be lost.")
        confirmText: qsTr("Discard edits")
        destructive: true
        onConfirmed: dialog.close()
    }

    FileDialog {
        id: imagePicker
        property string kind
        title: kind === "icon" ? qsTr("Choose channel icon") : qsTr("Choose channel banner")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp)")]
        onAccepted: { if (dialog.visible && dialog.canChangeArtwork) artworkCrop.openFor(dialog.channelId, kind, selectedFile) }
    }

    ArtworkCropDialog {
        id: artworkCrop
        onClosed: { if (dialog.visible) dialog.contentItem.forceActiveFocus() }
    }

    contentItem: ColumnLayout {
        spacing: Metrics.px(12)
        Text {
            text: dialog.title
            color: Theme.text
            font.pixelSize: Metrics.px(16)
            font.bold: true
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Metrics.px(6)
            Repeater {
                model: [qsTr("Overview"), qsTr("Appearance"), qsTr("Permissions")]
                delegate: FlatButton {
                    required property int index
                    required property string modelData
                    objectName: "channelSettingsSection" + index
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    leftPadding: Metrics.px(6)
                    rightPadding: Metrics.px(6)
                    text: modelData
                    primary: dialog.section === index
                    Accessible.name: modelData
                    Accessible.description: primary ? qsTr("Selected section") : qsTr("Open section")
                    onClicked: dialog.selectSection(index)
                    Keys.onPressed: event => {
                        let next = index
                        if (event.key === Qt.Key_Right) next = (index + 1) % 3
                        else if (event.key === Qt.Key_Left) next = (index + 2) % 3
                        else if (event.key === Qt.Key_Home) next = 0
                        else if (event.key === Qt.Key_End) next = 2
                        else return
                        dialog.selectSection(next)
                        sectionButtons.itemAt(next).forceActiveFocus()
                        event.accepted = true
                    }
                }
                id: sectionButtons
            }
        }
        ScrollView {
            id: formScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            clip: true
            objectName: "channelSettingsScroll"
            activeFocusOnTab: true
            Accessible.name: dialog.section === 2 ? qsTr("Your effective permissions") : qsTr("Channel settings form")
            Keys.onPressed: event => {
                if (dialog.section !== 2) return
                const view = contentItem as Flickable
                if (!view) return
                const maximum = Math.max(0, view.contentHeight - view.height)
                if (event.key === Qt.Key_End) view.contentY = maximum
                else if (event.key === Qt.Key_Home) view.contentY = 0
                else if (event.key === Qt.Key_Down) view.contentY = Math.min(maximum, view.contentY + Metrics.px(32))
                else if (event.key === Qt.Key_Up) view.contentY = Math.max(0, view.contentY - Metrics.px(32))
                else if (event.key === Qt.Key_PageDown) view.contentY = Math.min(maximum, view.contentY + view.height)
                else if (event.key === Qt.Key_PageUp) view.contentY = Math.max(0, view.contentY - view.height)
                else return
                event.accepted = true
            }
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: formScroll.availableWidth
                spacing: Metrics.px(12)
                ColumnLayout {
                    objectName: "channelSettingsOverview"
                    visible: dialog.section === 0
                    Layout.fillWidth: true
                    spacing: Metrics.px(12)
                Field {
                    id: nameField
                    enabled: dialog.canManage && !dialog.saving && !dialog.reloading && !dialog.artworkSaving && !dialog.artworkReloading
                    objectName: "channelName"
                    Layout.fillWidth: true
                    label: qsTr("Name")
                    input.maximumLength: 64
                }
                ComboBox {
                    id: categoryPicker
                    objectName: "channelCategory"
                    enabled: dialog.canManage && !dialog.saving && !dialog.reloading && !dialog.artworkSaving && !dialog.artworkReloading
                    Layout.fillWidth: true
                    visible: dialog.details.type !== "category"
                    model: App.channelCategories()
                    textRole: "name"
                    valueRole: "id"
                    onActivated: App.moveChannelToCategory(dialog.channelId, currentValue)
                    Accessible.name: qsTr("Category")
                }
                Field {
                    id: topicField
                    objectName: "channelTopic"
                    enabled: dialog.canManage && !dialog.saving && !dialog.reloading && !dialog.artworkSaving && !dialog.artworkReloading
                    Layout.fillWidth: true
                    label: qsTr("Topic")
                    hint: qsTr("Short summary shown in the channel header")
                    input.maximumLength: 512
                }
                Text {
                    text: qsTr("DESCRIPTION")
                    color: Theme.textMuted
                    font.pixelSize: Metrics.px(11)
                    font.bold: true
                }
                ScrollView {
                    objectName: "channelDescriptionScroll"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Metrics.px(150)
                    clip: true
                    TextArea {
                        id: descriptionField
                        objectName: "channelDescription"
                        readOnly: !dialog.canManage || dialog.saving || dialog.reloading || dialog.artworkSaving || dialog.artworkReloading
                        wrapMode: TextEdit.Wrap
                        color: Theme.text
                        placeholderText: qsTr("What is this channel for? Add guidance and useful links.")
                        font.pixelSize: Metrics.px(13)
                        background: Rectangle {
                            color: Theme.surfaceAlt
                            radius: Metrics.px(4)
                            border.color: descriptionField.activeFocus ? Theme.focus : Theme.controlBorder
                        }
                        Accessible.name: qsTr("Channel description")
                    }
                }
                Text {
                    Layout.alignment: Qt.AlignRight
                    text: descriptionField.length + "/2000"
                    color: descriptionField.length > 2000 ? Theme.danger : Theme.textFaint
                    font.pixelSize: Metrics.px(11)
                }
                Text {
                    objectName: "latestChannelDetails"
                    Layout.fillWidth: true
                    visible: dialog.latestLoaded
                    text: qsTr("Latest name: %1\nLatest topic: %2\nLatest description: %3")
                        .arg(dialog.details.name || "").arg(dialog.details.topic || "").arg(dialog.details.description || "")
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    color: Theme.textMuted
                    font.pixelSize: Metrics.px(12)
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Category changes apply immediately. Save applies to name, topic and description.")
                    wrapMode: Text.Wrap
                    color: Theme.textMuted
                    font.pixelSize: Metrics.px(12)
                }
                }
                ColumnLayout {
                    objectName: "channelSettingsAppearance"
                    visible: dialog.section === 1
                    Layout.fillWidth: true
                    spacing: Metrics.px(12)
                    Text {
                        Layout.fillWidth: true
                        text: App.capabilities.indexOf("channel.artwork.cas.v1") < 0
                            ? qsTr("This server needs an update before checked artwork can be saved.")
                            : qsTr("Icon and banner changes apply immediately after confirming the crop, or choosing Remove. Save does not apply to artwork.")
                        wrapMode: Text.Wrap
                        color: Theme.textMuted
                    }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Metrics.px(8)
                    Image {
                        source: dialog.details.icon_attachment_id && dialog.details.icon_attachment_id !== "0"
                                ? (App.previews[dialog.details.icon_attachment_id] || "") : ""
                        Layout.preferredWidth: Metrics.px(36)
                        Layout.preferredHeight: Metrics.px(36)
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                    FlatButton {
                        text: qsTr("Choose icon…")
                        id: chooseIcon
                        objectName: "channelChooseIcon"
                        onActiveFocusChanged: dialog.revealControl(chooseIcon)
                        enabled: dialog.canChangeArtwork && !dialog.saving && !dialog.reloading
                        onClicked: { imagePicker.kind = "icon"; imagePicker.open() }
                    }
                    FlatButton {
                        id: channelReframeIcon
                        objectName: "channelReframeIcon"
                        onActiveFocusChanged: if (activeFocus) dialog.revealControl(channelReframeIcon)
                        text: qsTr("Reframe")
                        visible: App.capabilities.indexOf("artwork.framing.v1") >= 0
                        enabled: dialog.canChangeArtwork && !dialog.saving && !dialog.reloading
                            && !!dialog.details.icon_attachment_id && dialog.details.icon_attachment_id !== "0"
                        onClicked: artworkCrop.openExisting(dialog.channelId, "icon", dialog.details.icon_attachment_id)
                    }
                    FlatButton {
                        objectName: "channelRemoveIcon"
                        text: qsTr("Remove")
                        enabled: dialog.canChangeArtwork && !dialog.saving && !dialog.reloading && !!dialog.details.icon_attachment_id && dialog.details.icon_attachment_id !== "0"
                        onClicked: dialog.removeArtwork("icon")
                    }
                }
                Image {
                    source: dialog.details.banner_attachment_id && dialog.details.banner_attachment_id !== "0"
                            ? (App.previews[dialog.details.banner_attachment_id] || "") : ""
                    Layout.fillWidth: true
                    Layout.preferredHeight: Metrics.px(76)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    visible: source.toString().length > 0
                }
                RowLayout {
                    Layout.fillWidth: true
                    FlatButton {
                        text: qsTr("Choose banner…")
                        id: chooseBanner
                        objectName: "channelChooseBanner"
                        onActiveFocusChanged: dialog.revealControl(chooseBanner)
                        enabled: dialog.canChangeArtwork && !dialog.saving && !dialog.reloading
                        onClicked: { imagePicker.kind = "banner"; imagePicker.open() }
                    }
                    FlatButton {
                        id: channelReframeBanner
                        objectName: "channelReframeBanner"
                        onActiveFocusChanged: if (activeFocus) dialog.revealControl(channelReframeBanner)
                        text: qsTr("Reframe")
                        visible: App.capabilities.indexOf("artwork.framing.v1") >= 0
                        enabled: dialog.canChangeArtwork && !dialog.saving && !dialog.reloading
                            && !!dialog.details.banner_attachment_id && dialog.details.banner_attachment_id !== "0"
                        onClicked: artworkCrop.openExisting(dialog.channelId, "banner", dialog.details.banner_attachment_id)
                    }
                    FlatButton {
                        objectName: "channelRemoveBanner"
                        text: qsTr("Remove")
                        enabled: dialog.canChangeArtwork && !dialog.saving && !dialog.reloading && !!dialog.details.banner_attachment_id && dialog.details.banner_attachment_id !== "0"
                        onClicked: dialog.removeArtwork("banner")
                    }
                }
                Text {
                    objectName: "channelArtworkStatus"
                    Layout.fillWidth: true
                    visible: dialog.artworkStatus.length > 0
                    text: dialog.artworkStatus
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    color: dialog.artworkNeedsReload ? Theme.danger : Theme.textMuted
                }
                FlatButton {
                    id: artworkReloadButton
                    objectName: "channelReloadArtwork"
                    text: qsTr("Reload artwork")
                    visible: dialog.artworkNeedsReload
                    enabled: dialog.canManage && !dialog.saving && !dialog.reloading && !dialog.artworkSaving && !dialog.artworkReloading
                    onActiveFocusChanged: dialog.revealControl(artworkReloadButton)
                    onClicked: dialog.reloadArtwork()
                }
                }
                ColumnLayout {
                    objectName: "channelSettingsPermissions"
                    visible: dialog.section === 2
                    Layout.fillWidth: true
                    spacing: Metrics.px(10)
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Your access")
                        color: Theme.text
                        font.bold: true
                        font.pixelSize: Metrics.px(14)
                    }
                    Text {
                        objectName: "channelSettingsPermissionSource"
                        Layout.fillWidth: true
                        text: dialog.review.source || ""
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        color: Theme.text
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: dialog.review.available
                        text: dialog.review.inheritance || ""
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        color: Theme.textMuted
                    }
                    Repeater {
                        model: dialog.review.rows || []
                        delegate: RowLayout {
                            id: permissionRow
                            required property var modelData
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: permissionRow.modelData.label
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                color: Theme.text
                            }
                            Text {
                                objectName: "channelSettingsPermission_" + permissionRow.modelData.name
                                text: permissionRow.modelData.allowed ? qsTr("Allowed") : qsTr("Denied")
                                color: Theme.text
                                Accessible.name: permissionRow.modelData.label + ": " + text
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: dialog.review.available
                        text: dialog.review.explanation || ""
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        color: Theme.textMuted
                    }
                    FlatButton {
                        id: editOverrides
                        objectName: "channelSettingsEditOverrides"
                        onActiveFocusChanged: dialog.revealControl(editOverrides)
                        text: qsTr("Edit role and member overrides…")
                        Layout.fillWidth: true
                        enabled: App.state === "connected" && App.canManageRoles && !dialog.saving && !dialog.reloading
                        onClicked: dialog.openPermissionsRequested(dialog.channelId, dialog.details.name || "")
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Override changes apply in their own editor. Your Overview draft stays here.")
                        wrapMode: Text.Wrap
                        color: Theme.textMuted
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            id: saveFeedback
            visible: dialog.saveStatus.length > 0
            text: dialog.saveStatus
            textFormat: Text.PlainText
            maximumLineCount: 3
            HoverHandler { id: statusHover }
            ToolTip.visible: saveFeedback.truncated && statusHover.hovered
            ToolTip.text: dialog.saveStatus
            elide: Text.ElideRight
            wrapMode: Text.Wrap
            color: dialog.saveFailed ? Theme.danger : Theme.textMuted
            font.pixelSize: Metrics.px(13)
        }
        Text {
            Layout.fillWidth: true
            visible: !dialog.checkedDetailsSupported
            text: qsTr("This server needs an update before checked channel details can be saved.")
            wrapMode: Text.Wrap
            color: Theme.danger
        }
        RowLayout {
            objectName: "channelSettingsActions"
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignRight
            FlatButton {
                id: reloadDetails
                objectName: "channelReloadDetails"
                text: dialog.reloading ? qsTr("Reloading…") : qsTr("Reload latest")
                visible: dialog.checkedDetailsSupported
                enabled: App.state === "connected" && !dialog.saving && !dialog.reloading && !dialog.artworkSaving && !dialog.artworkReloading
                onClicked: {
                    dialog.reloading = true
                    dialog.saveStatus = qsTr("Reloading latest details; retaining your edits…")
                    App.reloadChannelDetails(dialog.channelId)
                }
            }
            FlatButton { id: closeDetails; objectName: "channelCloseDetails"; text: qsTr("Close"); enabled: !dialog.saving && !dialog.reloading && !dialog.artworkSaving && !dialog.artworkReloading; onClicked: dialog.requestClose() }
            FlatButton {
                objectName: "channelSaveDetails"
                text: dialog.saving ? qsTr("Saving…") : qsTr("Save")
                primary: true
                enabled: dialog.canManage && dialog.checkedDetailsSupported && !dialog.needsReload && !dialog.reloading && !dialog.artworkSaving && !dialog.artworkReloading && dialog.dirty && !dialog.saving && nameField.text.trim().length > 0 && descriptionField.length <= 2000
                onClicked: {
                    dialog.submitted = {name: nameField.text, topic: topicField.text, description: descriptionField.text}
                    dialog.saving = true
                    dialog.saveFailed = false
                    dialog.saveStatus = qsTr("Saving channel details…")
                    App.updateChannelDetails(dialog.channelId, nameField.text, topicField.text,
                                             descriptionField.text, dialog.original)
                }
            }
        }
    }
}
