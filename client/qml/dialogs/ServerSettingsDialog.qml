import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import OmaChat

// Roles (name, color, permissions, order) and who holds them. The server
// checks rank and "cannot grant what you lack"; this only greys out what it
// would refuse.
Dialog {
    id: dialog
    title: qsTr("%1 settings").arg(App.selectedServerName)
    width: Math.min(Theme.px(760), (parent ? parent.width : 800) - Theme.px(40))
    height: Math.min(Theme.px(620), (parent ? parent.height : 600) - Theme.px(40))

    // ---- role editor state
    property string roleId
    property string editName
    property string editColor
    property var editPerms: []
    property bool dirty: false
    // ---- server overview state
    property string serverId
    property string serverName: ""
    property string serverDescription: ""
    property var serverDetails: ({})
    property bool serverDirty: false
    readonly property bool serverIdentitySupported: App.capabilities.indexOf("server.identity.v1") >= 0
    readonly property var role: {
        const roles = App.serverRoles
        for (let i = 0; i < roles.length; ++i)
            if (roles[i].id === roleId)
                return roles[i]
        return null
    }
    readonly property var swatches: ["", "#e06c75", "#d19a66", "#e5c07b", "#98c379", "#56b6c2", "#61afef", "#c678dd", "#abb2bf"]

    function selectRole(r) {
        roleId = r ? r.id : ""
        editName = r ? r.name : ""
        editColor = r && r.hasColor ? r.color : ""
        editPerms = r ? r.permissions.slice() : []
        dirty = false
    }
    function togglePerm(name, on) {
        const next = editPerms.filter(p => p !== name)
        if (on)
            next.push(name)
        editPerms = next
        dirty = true
    }

    onAboutToShow: {
        tabs.currentIndex = 0
        dialog.serverId = App.selectedServerId
        dialog.serverDetails = App.serverDetails(dialog.serverId)
        dialog.serverName = dialog.serverDetails.name || ""
        dialog.serverDescription = dialog.serverDetails.description || ""
        dialog.serverDirty = false
        dialog.loadServerImages()
        const roles = App.serverRoles
        selectRole(roles.length > 0 ? roles[0] : null)
    }

    function loadServerImages() {
        const icon = dialog.serverDetails.icon_attachment_id
        if (icon && icon !== "0")
            App.requestPreview(icon, "server-icon.png", 0)
        const banner = dialog.serverDetails.banner_attachment_id
        if (banner && banner !== "0")
            App.requestPreview(banner, "server-banner.png", 0)
    }

    Connections {
        target: App
        function onRolesChanged() {
            // Follow server-side changes unless the user is mid-edit.
            if (dialog.role === null) {
                const roles = App.serverRoles
                dialog.selectRole(roles.length > 0 ? roles[0] : null)
            } else if (!dialog.dirty) {
                dialog.selectRole(dialog.role)
            }
        }
        function onServerDataChanged(id) {
            if (id !== dialog.serverId)
                return
            dialog.serverDetails = App.serverDetails(id)
            if (!dialog.serverDirty) {
                dialog.serverName = dialog.serverDetails.name || ""
                dialog.serverDescription = dialog.serverDetails.description || ""
            }
            dialog.loadServerImages()
        }
        function onServerDetailsSaved(id) {
            if (id === dialog.serverId)
                dialog.serverDirty = false
        }
    }

    FileDialog {
        id: serverImagePicker
        property string kind
        title: kind === "icon" ? qsTr("Choose server icon") : qsTr("Choose server banner")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp)")]
        onAccepted: App.setServerArtwork(dialog.serverId, kind, selectedFile)
    }

    component Tab: TabButton {
        required property string modelData
        text: modelData
        font.pixelSize: Theme.px(13)
        contentItem: Text {
            text: parent.text
            font: parent.font
            color: parent.checked ? Theme.text : Theme.textMuted
            horizontalAlignment: Text.AlignHCenter
        }
        background: Rectangle {
            color: "transparent"
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 2
                color: parent.parent.checked ? Theme.accent : Theme.border
            }
        }
    }

    component RoleDot: Rectangle {
        property string roleColor
        implicitWidth: Theme.px(10)
        implicitHeight: Theme.px(10)
        radius: width / 2
        color: roleColor.length > 0 ? roleColor : Theme.textFaint
    }

    contentItem: ColumnLayout {
        spacing: Theme.px(10)

        RowLayout {
            Text { text: dialog.title; color: Theme.text; font.pixelSize: Theme.px(16); font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
            IconButton { iconName: "x"; tip: qsTr("Close"); onClicked: dialog.close() }
        }

        TabBar {
            id: tabs
            Layout.fillWidth: true
            background: Rectangle { color: "transparent" }
            Repeater {
                model: [qsTr("Overview"), qsTr("Roles"), qsTr("Members"), qsTr("Emoji")]
                delegate: Tab {}
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // ---------------------------------------------------- overview
            ScrollView {
                clip: true
                ColumnLayout {
                    width: parent.width
                    spacing: Theme.px(10)

                    Field {
                        Layout.fillWidth: true
                        label: qsTr("Server name")
                        text: dialog.serverName
                        input.maximumLength: 100
                        input.enabled: App.canManageServer
                        input.onTextEdited: { dialog.serverName = input.text; dialog.serverDirty = true }
                    }
                    Text {
                        text: qsTr("DESCRIPTION")
                        color: Theme.textMuted
                        font.pixelSize: Theme.px(11)
                        font.bold: true
                    }
                    TextArea {
                        id: serverDescriptionField
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.px(120)
                        wrapMode: TextEdit.Wrap
                        color: Theme.text
                        text: dialog.serverDescription
                        enabled: App.canManageServer
                        placeholderText: qsTr("What is this server for? Add guidance and useful links.")
                        font.pixelSize: Theme.px(13)
                        background: Rectangle {
                            color: Theme.surfaceAlt
                            radius: Theme.px(4)
                            border.color: serverDescriptionField.activeFocus ? Theme.accent : Theme.border
                        }
                        onTextChanged: {
                            if (dialog.serverDescription !== text) {
                                dialog.serverDescription = text
                                dialog.serverDirty = true
                            }
                        }
                        Accessible.name: qsTr("Server description")
                    }
                    Text {
                        Layout.alignment: Qt.AlignRight
                        text: (dialog.serverDescription || "").length + "/2000"
                        color: (dialog.serverDescription || "").length > 2000 ? Theme.danger : Theme.textFaint
                        font.pixelSize: Theme.px(11)
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.px(8)
                        enabled: dialog.serverIdentitySupported && App.canManageServer
                        Image {
                            source: dialog.serverDetails.icon_attachment_id
                                    && dialog.serverDetails.icon_attachment_id !== "0"
                                    ? (App.previews[dialog.serverDetails.icon_attachment_id] || "") : ""
                            Layout.preferredWidth: Theme.px(36)
                            Layout.preferredHeight: Theme.px(36)
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                        }
                        FlatButton {
                            text: qsTr("Choose icon…")
                            onClicked: { serverImagePicker.kind = "icon"; serverImagePicker.open() }
                        }
                        FlatButton {
                            text: qsTr("Remove icon")
                            enabled: !!dialog.serverDetails.icon_attachment_id
                                     && dialog.serverDetails.icon_attachment_id !== "0"
                            onClicked: App.setServerArtwork(dialog.serverId, "icon", "")
                        }
                    }
                    Image {
                        source: dialog.serverDetails.banner_attachment_id
                                && dialog.serverDetails.banner_attachment_id !== "0"
                                ? (App.previews[dialog.serverDetails.banner_attachment_id] || "") : ""
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.px(76)
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        visible: source.toString().length > 0
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        enabled: dialog.serverIdentitySupported && App.canManageServer
                        FlatButton {
                            text: qsTr("Choose banner…")
                            onClicked: { serverImagePicker.kind = "banner"; serverImagePicker.open() }
                        }
                        FlatButton {
                            text: qsTr("Remove banner")
                            enabled: !!dialog.serverDetails.banner_attachment_id
                                     && dialog.serverDetails.banner_attachment_id !== "0"
                            onClicked: App.setServerArtwork(dialog.serverId, "banner", "")
                        }
                    }
                    Text {
                        visible: !dialog.serverIdentitySupported
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.textFaint
                        font.pixelSize: Theme.px(11)
                        text: qsTr("This server does not support icons, banners, or descriptions yet.")
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Item { Layout.fillWidth: true }
                        FlatButton {
                            text: qsTr("Revert")
                            enabled: dialog.serverDirty
                            onClicked: {
                                dialog.serverName = dialog.serverDetails.name || ""
                                dialog.serverDescription = dialog.serverDetails.description || ""
                                dialog.serverDirty = false
                            }
                        }
                        FlatButton {
                            text: qsTr("Save")
                            primary: true
                            enabled: dialog.serverDirty && App.canManageServer
                                     && dialog.serverName.trim().length > 0
                                     && (dialog.serverDescription || "").length <= 2000
                            onClicked: App.updateServerDetails(dialog.serverId, dialog.serverName,
                                                               dialog.serverDescription)
                        }
                    }
                }
            }

            // ------------------------------------------------------ roles
            RowLayout {
                spacing: Theme.px(14)

                ColumnLayout {
                    Layout.preferredWidth: Theme.px(220)
                    Layout.fillHeight: true
                    spacing: Theme.px(6)

                    ListView {
                        id: roleList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: App.serverRoles
                        spacing: Theme.px(2)
                        delegate: Rectangle {
                            id: roleRow
                            required property var modelData
                            required property int index
                            width: ListView.view.width
                            implicitHeight: Theme.px(32)
                            radius: Theme.px(4)
                            color: dialog.roleId === modelData.id ? Theme.selection : rowArea.containsMouse ? Theme.raised : "transparent"
                            MouseArea {
                                id: rowArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: dialog.selectRole(roleRow.modelData)
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.px(8)
                                anchors.rightMargin: Theme.px(2)
                                spacing: Theme.px(6)
                                RoleDot { roleColor: roleRow.modelData.hasColor ? roleRow.modelData.color : "" }
                                Text {
                                    Layout.fillWidth: true
                                    text: roleRow.modelData.isDefault ? qsTr("@everyone") : roleRow.modelData.name
                                    color: Theme.text
                                    elide: Text.ElideRight
                                    font.pixelSize: Theme.px(13)
                                }
                                Text {
                                    text: roleRow.modelData.members
                                    color: Theme.textFaint
                                    font.pixelSize: Theme.px(11)
                                }
                                IconButton {
                                    visible: roleRow.modelData.editable && !roleRow.modelData.isDefault
                                    implicitWidth: Theme.px(22)
                                    implicitHeight: Theme.px(22)
                                    iconSize: Theme.px(12)
                                    iconName: "chevron-down"
                                    rotation: 180
                                    tip: qsTr("Move up")
                                    enabled: roleRow.index > 0 && App.serverRoles[roleRow.index - 1].editable
                                    onClicked: App.moveRole(roleRow.modelData.id, 1)
                                }
                                IconButton {
                                    visible: roleRow.modelData.editable && !roleRow.modelData.isDefault
                                    implicitWidth: Theme.px(22)
                                    implicitHeight: Theme.px(22)
                                    iconSize: Theme.px(12)
                                    iconName: "chevron-down"
                                    tip: qsTr("Move down")
                                    enabled: roleRow.index + 1 < App.serverRoles.length && !App.serverRoles[roleRow.index + 1].isDefault
                                    onClicked: App.moveRole(roleRow.modelData.id, -1)
                                }
                            }
                        }
                    }
                    FlatButton {
                        Layout.fillWidth: true
                        text: qsTr("New role")
                        enabled: App.canManageRoles
                        onClicked: newRole.open()
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.textFaint
                        font.pixelSize: Theme.px(11)
                        text: qsTr("Higher roles outrank lower ones. You can only edit roles below your own.")
                    }
                }

                Rectangle { Layout.fillHeight: true; implicitWidth: 1; color: Theme.border }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: Theme.px(10)
                    visible: dialog.role !== null
                    readonly property bool canEdit: dialog.role !== null && dialog.role.editable

                    Field {
                        Layout.fillWidth: true
                        label: qsTr("Role name")
                        text: dialog.role && dialog.role.isDefault ? qsTr("@everyone") : dialog.editName
                        input.enabled: parent.canEdit && dialog.role && !dialog.role.isDefault
                        input.onTextEdited: { dialog.editName = input.text; dialog.dirty = true }
                    }

                    Text {
                        visible: !(dialog.role && dialog.role.isDefault)
                        text: qsTr("COLOR")
                        color: Theme.textMuted
                        font.pixelSize: Theme.px(11)
                        font.bold: true
                    }
                    Flow {
                        visible: !(dialog.role && dialog.role.isDefault)
                        Layout.fillWidth: true
                        spacing: Theme.px(6)
                        Repeater {
                            model: dialog.swatches
                            delegate: Rectangle {
                                required property string modelData
                                width: Theme.px(24)
                                height: Theme.px(24)
                                radius: Theme.px(4)
                                color: modelData.length > 0 ? modelData : Theme.surfaceAlt
                                border.width: dialog.editColor.toLowerCase() === modelData ? 2 : 1
                                border.color: dialog.editColor.toLowerCase() === modelData ? Theme.text : Theme.border
                                Text {
                                    anchors.centerIn: parent
                                    visible: parent.modelData.length === 0
                                    text: "∅"
                                    color: Theme.textMuted
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    enabled: dialog.role !== null && dialog.role.editable
                                    onClicked: { dialog.editColor = parent.modelData; dialog.dirty = true }
                                }
                                Accessible.role: Accessible.Button
                                Accessible.name: modelData.length > 0 ? modelData : qsTr("No color")
                            }
                        }
                    }

                    Text {
                        text: qsTr("PERMISSIONS")
                        color: Theme.textMuted
                        font.pixelSize: Theme.px(11)
                        font.bold: true
                    }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        ColumnLayout {
                            width: parent.width
                            spacing: Theme.px(2)
                            Repeater {
                                model: App.permissionCatalog
                                delegate: ColumnLayout {
                                    id: permRow
                                    required property var modelData
                                    required property int index
                                    Layout.fillWidth: true
                                    spacing: 0
                                    SectionLabel {
                                        visible: permRow.index === 0 || App.permissionCatalog[permRow.index - 1].group !== permRow.modelData.group
                                        text: permRow.modelData.group
                                        Layout.topMargin: Theme.px(6)
                                    }
                                    Check {
                                        text: permRow.modelData.label
                                        description: permRow.modelData.description
                                        checked: dialog.editPerms.indexOf(permRow.modelData.name) >= 0
                                        enabled: dialog.role !== null && dialog.role.editable
                                        onToggled: dialog.togglePerm(permRow.modelData.name, checked)
                                    }
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.px(8)
                        FlatButton {
                            text: qsTr("Delete role")
                            danger: true
                            visible: dialog.role !== null && !dialog.role.isDefault
                            enabled: dialog.role !== null && dialog.role.editable
                            onClicked: deleteConfirm.open()
                        }
                        Item { Layout.fillWidth: true }
                        FlatButton {
                            text: qsTr("Revert")
                            enabled: dialog.dirty
                            onClicked: dialog.selectRole(dialog.role)
                        }
                        FlatButton {
                            text: qsTr("Save")
                            primary: true
                            enabled: dialog.dirty && dialog.role !== null && dialog.role.editable
                            onClicked: {
                                App.updateRole(dialog.roleId, dialog.role.isDefault ? "" : dialog.editName,
                                               dialog.editColor.replace("#", ""), dialog.editPerms)
                                dialog.dirty = false
                            }
                        }
                    }
                }

                Text {
                    visible: dialog.role === null
                    Layout.fillWidth: true
                    text: qsTr("Select a role")
                    color: Theme.textFaint
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            // ---------------------------------------------------- members
            ListView {
                clip: true
                model: App.serverMembers
                spacing: Theme.px(4)
                delegate: Rectangle {
                    id: memberRow
                    required property var modelData
                    width: ListView.view.width
                    implicitHeight: memberCol.implicitHeight + Theme.px(12)
                    radius: Theme.px(4)
                    color: Theme.surfaceAlt
                    ColumnLayout {
                        id: memberCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: Theme.px(8)
                        spacing: Theme.px(4)
                        RowLayout {
                            spacing: Theme.px(6)
                            Avatar { userId: memberRow.modelData.userId; name: memberRow.modelData.name; size: Theme.px(22) }
                            Text {
                                text: memberRow.modelData.name
                                color: App.userColor(memberRow.modelData.userId)
                                font.bold: true
                                font.pixelSize: Theme.px(13)
                            }
                            Icon { visible: memberRow.modelData.isOwner; name: "crown"; size: Theme.px(12) }
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.px(6)
                            Repeater {
                                model: App.serverRoles.filter(r => !r.isDefault)
                                delegate: Rectangle {
                                    id: chip
                                    required property var modelData
                                    readonly property bool held: memberRow.modelData.roles.indexOf(modelData.id) >= 0
                                    readonly property bool canToggle: memberRow.modelData.editable && modelData.editable
                                    visible: held || canToggle
                                    implicitWidth: chipRow.implicitWidth + Theme.px(14)
                                    implicitHeight: Theme.px(24)
                                    radius: height / 2
                                    color: held ? Theme.selection : "transparent"
                                    border.color: held ? (modelData.hasColor ? modelData.color : Theme.accent) : Theme.border
                                    opacity: canToggle ? 1 : 0.8
                                    Row {
                                        id: chipRow
                                        anchors.centerIn: parent
                                        spacing: Theme.px(5)
                                        RoleDot { anchors.verticalCenter: parent.verticalCenter; roleColor: chip.modelData.hasColor ? chip.modelData.color : "" }
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: chip.modelData.name
                                            color: chip.held ? Theme.text : Theme.textMuted
                                            font.pixelSize: Theme.px(12)
                                        }
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        enabled: chip.canToggle
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: App.setMemberRole(memberRow.modelData.userId, chip.modelData.id, !chip.held)
                                    }
                                    Accessible.role: Accessible.CheckBox
                                    Accessible.name: chip.modelData.name
                                    Accessible.checked: chip.held
                                }
                            }
                        }
                    }
                }
            }

            // ------------------------------------------------------ emoji
            ColumnLayout {
                spacing: Theme.px(10)

                RowLayout {
                    visible: App.canManageEmoji
                    Layout.fillWidth: true
                    spacing: Theme.px(8)
                    Field {
                        id: newEmojiName
                        Layout.fillWidth: true
                        placeholder: qsTr("Emoji name (letters, numbers, underscore)")
                    }
                    FlatButton {
                        text: newEmojiFile.selectedFile ? qsTr("Image chosen") : qsTr("Choose image…")
                        onClicked: newEmojiFile.open()
                    }
                    FlatButton {
                        primary: true
                        text: qsTr("Upload")
                        enabled: newEmojiName.text.trim().length >= 2 && String(newEmojiFile.selectedFile).length > 0
                        onClicked: {
                            App.createServerEmoji(newEmojiName.text.trim(), newEmojiFile.selectedFile)
                            newEmojiName.text = ""
                            newEmojiFile.selectedFile = undefined
                        }
                    }
                }

                FileDialog {
                    id: newEmojiFile
                    title: qsTr("Choose an emoji image")
                    nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.gif *.webp)")]
                }

                GridView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    cellWidth: Theme.px(96)
                    cellHeight: Theme.px(96)
                    model: App.serverEmoji
                    delegate: Column {
                        id: emojiCell
                        required property var modelData
                        width: Theme.px(90)
                        height: Theme.px(90)
                        spacing: Theme.px(4)
                        Component.onCompleted: App.requestMedia(modelData.attachment_id, modelData.name)
                        Image {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: Theme.px(40)
                            height: Theme.px(40)
                            fillMode: Image.PreserveAspectFit
                            source: App.previews[emojiCell.modelData.attachment_id] || ""
                        }
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideMiddle
                            color: Theme.textMuted
                            font.pixelSize: Theme.px(11)
                            text: ":" + emojiCell.modelData.name + ":"
                        }
                        FlatButton {
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: App.canManageEmoji
                            danger: true
                            text: qsTr("Delete")
                            onClicked: App.deleteServerEmoji(emojiCell.modelData.id)
                        }
                    }
                }

                Text {
                    visible: App.serverEmoji.length === 0
                    Layout.fillWidth: true
                    text: qsTr("No custom emoji yet")
                    color: Theme.textFaint
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }

    TextPromptDialog {
        id: newRole
        title: qsTr("New role")
        label: qsTr("Role name")
        acceptText: qsTr("Create")
        onAccepted: value => App.createRole(value)
    }

    ConfirmDialog {
        id: deleteConfirm
        title: qsTr("Delete %1?").arg(dialog.role ? dialog.role.name : "")
        message: qsTr("Everyone holding this role loses it.")
        confirmText: qsTr("Delete role")
        destructive: true
        onConfirmed: App.deleteRole(dialog.roleId)
    }
}
