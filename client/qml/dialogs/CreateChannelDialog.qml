import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import OmaChat

Dialog {
    id: dialog
    property bool advanced: false
    property bool reviewingAccess: false
    property var accessReview: ({})
    property string previewUserId: ""
    property string previewRoleId: ""
    readonly property var preview: App.permissionPreview
    readonly property string reviewParent: channelType === "category" ? "0" : (categoryPicker.currentValue || "0")
    readonly property bool previewMatches: preview.newChannel && preview.serverId === App.selectedServerId
        && preview.channelType === channelType && preview.parentId === reviewParent
        && preview.roleId === previewRoleId && preview.userId === (previewRoleId ? "0" : (previewUserId || App.selfId))
    readonly property bool fallbackReview: App.capabilities.indexOf("permission_preview_creation") < 0
        && !previewUserId && !previewRoleId
    readonly property var accessRows: App.state !== "connected" ? [] : fallbackReview ? (accessReview.rows || [])
        : previewMatches && preview.available && !preview.loading && !preview.error ? (preview.rows || []) : []
    readonly property var previewSubjects: {
        const subjects = [{ userId: "", name: qsTr("Your access") }]
        if (App.canManageRoles) {
            for (const member of App.serverMembers)
                if (member.userId !== App.selfId) subjects.push({ userId: member.userId, name: member.name })
            if (App.capabilities.indexOf("permission_preview_roles") >= 0)
                for (const role of App.serverRoles)
                    subjects.push({ userId: "", roleId: role.id, name: qsTr("Role only: %1").arg(role.name) })
        }
        return subjects
    }
    readonly property string accessSource: {
        if (App.state !== "connected") return qsTr("Connect to review inherited access.")
        if (fallbackReview) return accessReview.source || ""
        if (!previewMatches || preview.loading) return qsTr("Loading inherited permission sources…")
        if (preview.error) return preview.error
        if (!preview.available) return qsTr("Inherited permission sources are unavailable.")
        if (previewRoleId) return qsTr("Hypothetical role access: @everyone and this role, with no real member, owner bypass or member-specific overrides. Confirmed by the server.")
        return qsTr("Inherited access for the selected member, confirmed by the server. The new channel has no overrides; permissions may change before creation.")
    }
    readonly property string channelType: voiceType.checked ? "voice" : categoryType.checked ? "category" : "text"
    function refreshAccessReview() {
        accessReview = App.channelCreationAccessReview(channelType, reviewParent)
        if (!App.canManageRoles) { previewUserId = ""; previewRoleId = "" }
        if (visible && reviewingAccess)
            App.loadCreationPermissionPreview(channelType, reviewParent, previewUserId, previewRoleId)
    }
    onChannelTypeChanged: refreshAccessReview()
    onPreviewUserIdChanged: refreshAccessReview()
    onPreviewRoleIdChanged: refreshAccessReview()
    property bool saving: false
    property string saveError: ""
    property bool created: false
    property string accountId: ""
    property string serverId: ""
    property string initialCategory: "0"
    readonly property bool dirty: nameField.text.length > 0 || topicField.text.length > 0
        || descriptionField.text.length > 0 || iconFile.toString().length > 0 || bannerFile.toString().length > 0
        || !textType.checked || categoryPicker.currentValue !== initialCategory
    protectClose: saving || (!created && dirty)
    onCloseRequested: { if (!saving) discardConfirm.open() }
    property string parentId
    property url iconFile
    property url bannerFile
    title: qsTr("Create channel")
    width: Math.min(Metrics.px(500), (parent ? parent.width : 800) - Metrics.px(40))
    padding: Metrics.px(12)
    height: Math.min(Metrics.px(700), (parent ? parent.height : 800) - Metrics.px(24))

    onAboutToShow: {
        clearArtwork()
        accountId = App.accountId
        serverId = App.selectedServerId
        created = false
        advanced = false
        reviewingAccess = false
        previewUserId = ""
        previewRoleId = ""
        saving = false
        saveError = ""
        nameField.text = ""
        topicField.text = ""
        descriptionField.text = ""
        iconFile = ""
        bannerFile = ""
        textType.checked = true
        const categories = App.channelCategories()
        categoryPicker.currentIndex = 0
        for (let i = 0; i < categories.length; ++i)
            if (categories[i].id === dialog.parentId)
                categoryPicker.currentIndex = i
        initialCategory = categoryPicker.currentValue || "0"
        refreshAccessReview()
    }
    onOpened: nameField.input.forceActiveFocus()
    onClosed: {
        artworkPicker.close()
        cropDialog.uploading = false
        cropDialog.close()
        discardConfirm.close()
        clearArtwork()
    }
    function clearArtwork() {
        App.discardStagedArtwork(iconFile)
        App.discardStagedArtwork(bannerFile)
        iconFile = ""
        bannerFile = ""
    }
    function clearKind(kind) {
        if (kind === "icon") { App.discardStagedArtwork(iconFile); iconFile = "" }
        else { App.discardStagedArtwork(bannerFile); bannerFile = "" }
    }
    function invalidate() {
        if (!visible) return
        saving = false
        close()
    }

    function create() {
        const name = nameField.text.trim()
        if (saving || created || cropDialog.visible || artworkPicker.visible || !App.ready
            || App.accountId !== accountId || App.selectedServerId !== serverId || !name || descriptionField.length > 2000)
            return
        const type = voiceType.checked ? "voice" : categoryType.checked ? "category" : "text"
        saving = true
        saveError = ""
        App.createChannel(name, type, type === "category" ? "0" : categoryPicker.currentValue,
                          topicField.text, descriptionField.text, iconFile, bannerFile)
    }

    Connections {
        target: App
        function onChannelDataChanged(id) { if (dialog.visible) dialog.refreshAccessReview() }
        function onServerDataChanged(id) { if (dialog.visible) dialog.refreshAccessReview() }
        function onRolesChanged() { if (dialog.visible) dialog.refreshAccessReview() }
        function onArtworkGenerationChanged() { dialog.invalidate() }
        function onStatusChanged() {
            if (App.accountId !== dialog.accountId) dialog.invalidate()
            else if (dialog.visible) dialog.refreshAccessReview()
        }
        function onSelectionChanged() { if (App.selectedServerId !== dialog.serverId) dialog.invalidate() }
        function onAdministrationFinished(operation, id, error, saved) {
            if (operation !== "channel.create" || !dialog.saving) return
            dialog.saving = false
            dialog.saveError = error
            dialog.created = Boolean(saved && saved.created)
            if (!error) dialog.close()
        }
    }

    FileDialog {
        id: artworkPicker
        property string kind
        title: kind === "icon" ? qsTr("Choose channel icon") : qsTr("Choose channel banner")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp)")]
        onAccepted: {
            if (dialog.visible && !dialog.saving && App.accountId === dialog.accountId
                && App.selectedServerId === dialog.serverId)
                cropDialog.openFor("", kind, selectedFile)
        }
    }

    ArtworkCropDialog {
        id: cropDialog
        target: "creation"
        onCropSelected: (kind, token) => {
            dialog.clearKind(kind)
            if (kind === "icon") dialog.iconFile = token
            else dialog.bannerFile = token
        }
    }
    Connections {
        target: cropDialog
        function onClosed() {
            if (dialog.visible && !dialog.saving && !dialog.created)
                nameField.input.forceActiveFocus()
        }
    }
    ConfirmDialog {
        id: discardConfirm
        objectName: "discardChannelCreation"
        title: qsTr("Discard channel draft?")
        message: qsTr("Your channel details and approved artwork crops will be lost.")
        confirmText: qsTr("Discard draft")
        destructive: true
        onConfirmed: dialog.close()
        onClosed: { if (dialog.visible) nameField.input.forceActiveFocus() }
    }

    contentItem: ColumnLayout {
        spacing: Metrics.px(8)
        Text { text: dialog.title; color: Theme.text; font.pixelSize: Metrics.px(16); font.bold: true }
        ScrollView {
            id: formScroll
            objectName: "creationFormScroll"
            activeFocusOnTab: true
            Accessible.name: qsTr("Channel details and inherited access")
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            clip: true
            enabled: !dialog.saving && !dialog.created
            Flickable {
                id: formView
                objectName: "creationFormViewport"
                contentWidth: width
                contentHeight: formContent.implicitHeight
                clip: true
                activeFocusOnTab: true
                Accessible.name: qsTr("Channel details and inherited access")
                Keys.onPressed: event => {
                    const view = formView
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
                ColumnLayout {
                    id: formContent
                    width: formView.width
                    spacing: Metrics.px(8)
                    RowLayout {
                        Layout.fillWidth: true
                        RadioButton { id: textType; text: qsTr("Text"); checked: true; palette.windowText: Theme.text }
                        RadioButton { id: voiceType; text: qsTr("Voice"); palette.windowText: Theme.text }
                        RadioButton { id: categoryType; text: qsTr("Category"); palette.windowText: Theme.text }
                    }
                    Field {
                        id: nameField
                        objectName: "channelName"
                        Layout.fillWidth: true
                        label: qsTr("Name")
                        placeholder: voiceType.checked ? qsTr("Lounge") : categoryType.checked ? qsTr("Projects") : qsTr("new-channel")
                        input.maximumLength: 64
                        input.onAccepted: dialog.create()
                    }
                    ComboBox {
                        id: categoryPicker
                        Layout.fillWidth: true
                        visible: !categoryType.checked
                        model: App.channelCategories()
                        textRole: "name"
                        valueRole: "id"
                        Accessible.name: qsTr("Category")
                        onCurrentValueChanged: dialog.refreshAccessReview()
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: dialog.advanced
                        spacing: Metrics.px(8)
                        Field {
                            id: topicField
                            objectName: "channelTopic"
                            Layout.fillWidth: true
                            label: qsTr("Topic")
                            hint: qsTr("Short summary shown in the header")
                            input.maximumLength: 512
                        }
                        Text { text: qsTr("DESCRIPTION"); color: Theme.textMuted; font.pixelSize: Metrics.px(11); font.bold: true }
                        TextArea {
                            id: descriptionField
                            Layout.fillWidth: true
                            Layout.preferredHeight: Metrics.px(100)
                            wrapMode: TextEdit.Wrap
                            color: Theme.text
                            placeholderText: qsTr("What is this channel for?")
                            font.pixelSize: Metrics.px(13)
                            background: Rectangle {
                                color: Theme.surfaceAlt
                                radius: Metrics.px(4)
                                border.color: descriptionField.activeFocus ? Theme.focus : Theme.controlBorder
                            }
                            Accessible.name: qsTr("Channel description")
                        }
                        Text {
                            Layout.alignment: Qt.AlignRight
                            text: descriptionField.length + "/2000"
                            color: descriptionField.length > 2000 ? Theme.danger : Theme.textFaint
                            font.pixelSize: Metrics.px(11)
                        }
                        RowLayout {
                            FlatButton { text: qsTr("Choose icon…"); onClicked: { artworkPicker.kind = "icon"; artworkPicker.open() } }
                            Text { text: dialog.iconFile.toString().length > 0 ? qsTr("Crop approved") : ""; color: Theme.textMuted }
                            FlatButton { text: qsTr("Clear"); visible: dialog.iconFile.toString().length > 0; onClicked: dialog.clearKind("icon") }
                        }
                        RowLayout {
                            FlatButton { text: qsTr("Choose banner…"); onClicked: { artworkPicker.kind = "banner"; artworkPicker.open() } }
                            Text { text: dialog.bannerFile.toString().length > 0 ? qsTr("Crop approved") : ""; color: Theme.textMuted }
                            FlatButton { text: qsTr("Clear"); visible: dialog.bannerFile.toString().length > 0; onClicked: dialog.clearKind("banner") }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("New channels inherit server roles and category overrides. Review your inherited access below before creating.")
                            wrapMode: Text.Wrap
                            color: Theme.textMuted
                            font.pixelSize: Metrics.px(11)
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: dialog.reviewingAccess
                        spacing: Metrics.px(8)
                        ComboBox {
                            objectName: "creationPermissionSubjectPicker"
                            Layout.fillWidth: true
                            visible: App.canManageRoles && !dialog.fallbackReview
                            enabled: App.state === "connected" && dialog.previewMatches && dialog.preview.canPreviewMembers
                            model: dialog.previewSubjects
                            textRole: "name"
                            Accessible.name: qsTr("Member or hypothetical role whose inherited access to review")
                            currentIndex: {
                                for (let i = 0; i < dialog.previewSubjects.length; ++i)
                                    if (dialog.previewSubjects[i].userId === dialog.previewUserId
                                        && (dialog.previewSubjects[i].roleId || "") === dialog.previewRoleId) return i
                                return 0
                            }
                            onActivated: index => {
                                const subject = dialog.previewSubjects[index]
                                dialog.previewUserId = subject.userId
                                dialog.previewRoleId = subject.roleId || ""
                            }
                        }
                        Text {
                            objectName: "creationAccessSource"
                            Layout.fillWidth: true
                            text: dialog.accessSource
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            color: Theme.text
                            font.pixelSize: Metrics.px(13)
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: dialog.fallbackReview && dialog.accessReview.available
                            text: dialog.accessReview.inheritance || ""
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            color: Theme.textMuted
                            font.pixelSize: Metrics.px(12)
                        }
                        Repeater {
                            model: dialog.accessRows
                            delegate: ColumnLayout {
                                id: creationAccessRow
                                required property var modelData
                                Layout.fillWidth: true
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        Layout.fillWidth: true
                                        text: creationAccessRow.modelData.label
                                        textFormat: Text.PlainText
                                        wrapMode: Text.Wrap
                                        color: Theme.text
                                        font.pixelSize: Metrics.px(12)
                                    }
                                    Text {
                                        objectName: "creationPermission_" + creationAccessRow.modelData.name
                                        text: creationAccessRow.modelData.allowed ? qsTr("Allowed") : qsTr("Denied")
                                        color: Theme.text
                                        font.pixelSize: Metrics.px(12)
                                        textFormat: Text.PlainText
                                    }
                                }
                                Text {
                                    objectName: "creationPermissionSource_" + creationAccessRow.modelData.name
                                    Layout.fillWidth: true
                                    visible: !dialog.fallbackReview
                                    text: creationAccessRow.modelData.source || ""
                                    textFormat: Text.PlainText
                                    wrapMode: Text.Wrap
                                    color: Theme.textMuted
                                    font.pixelSize: Metrics.px(12)
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: dialog.fallbackReview
                            text: dialog.accessReview.explanation || ""
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            color: Theme.textMuted
                            font.pixelSize: Metrics.px(12)
                        }
                    }
                }
            }
        }
        FlatButton {
            objectName: "reviewCreationAccess"
            text: dialog.reviewingAccess ? qsTr("Hide access review") : qsTr("Review inherited access…")
            enabled: !dialog.saving && !dialog.created
            onClicked: {
                dialog.reviewingAccess = !dialog.reviewingAccess
                dialog.refreshAccessReview()
                if (dialog.reviewingAccess) formView.forceActiveFocus(Qt.TabFocusReason)
            }
        }
        FlatButton {
            objectName: "advancedOptions"
            text: dialog.advanced ? qsTr("Hide optional settings") : qsTr("Optional settings…")
            onClicked: dialog.advanced = !dialog.advanced
        }
        Text {
            Layout.fillWidth: true
            visible: dialog.saveError.length > 0
            text: dialog.saveError
            color: Theme.danger
            wrapMode: Text.Wrap
            font.pixelSize: Metrics.px(13)
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Metrics.px(8)
            FlatButton { text: dialog.created ? qsTr("Close") : qsTr("Cancel"); enabled: !dialog.saving; onClicked: dialog.requestClose() }
            FlatButton {
                objectName: "channelCreate"
                primary: true
                text: dialog.saving ? qsTr("Creating…") : qsTr("Create")
                enabled: !dialog.saving && !dialog.created && App.ready && nameField.text.trim().length > 0 && descriptionField.length <= 2000
                onClicked: dialog.create()
            }
        }
    }
}
