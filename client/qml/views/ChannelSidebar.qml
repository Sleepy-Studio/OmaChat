import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// Categories, text and voice channels (with live participants), or direct
// messages when "Direct Messages" is selected in the rail.
Rectangle {
    id: sidebar
    color: Theme.surface

    signal createChannel(string parentId)
    signal openServerSettings()
    signal openChannelPermissions(string channelId, string name)
    signal openChannelSettings(string channelId)
    signal openChannelDetails(string channelId)
    signal newGroup()
    signal addToGroup(string channelId)
    signal renameGroup(string channelId, string name)

    function focusList() {
        list.forceActiveFocus()
        const i = App.channels.indexOf("itemId", App.selectedChannelId)
        list.currentIndex = i >= 0 ? i : 0
    }

    function openContextMenu(index, item) {
        if (index < 0 || index >= list.count) return
        const row = list.model.get(index)
        if (row.rowType === "participant") return
        list.currentIndex = index
        const menu = row.rowType === "category" ? categoryMenu : channelMenu
        menu.channelId = row.itemId
        menu.channelName = row.name
        if (menu === channelMenu) {
            menu.isVoice = row.rowType === "voice"
            menu.isGroup = row.rowType === "group_dm"
            menu.userId = row.userId || ""
        }
        if (item) {
            const point = item.mapToItem(sidebar, 0, item.height)
            menu.popup(sidebar, point.x, point.y)
        } else {
            menu.popup()
        }
    }

    function loadServerBanner() {
        const id = App.selectedServerBannerId
        if (!App.homeSelected && id && id !== "0")
            App.requestPreview(id, "server-banner.png", 0)
    }

    Component.onCompleted: loadServerBanner()
    Connections {
        target: App
        function onSelectionChanged() { sidebar.loadServerBanner() }
        function onServerDataChanged(id) {
            if (id === App.selectedServerId)
                sidebar.loadServerBanner()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header: server name + actions
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Metrics.px(46)
            color: headerArea.containsMouse && !App.homeSelected ? Theme.raised : "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Metrics.px(14)
                anchors.rightMargin: Metrics.px(6)
                Text {
                    Layout.fillWidth: true
                    text: App.selectedServerName
                    color: Theme.text
                    font.pixelSize: Metrics.px(15)
                    font.bold: true
                    elide: Text.ElideRight
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                IconButton {
                    visible: App.canCreateInvites
                    iconName: "users"
                    tip: qsTr("Invite people")
                    onClicked: App.createInvite()
                }
                IconButton {
                    visible: App.homeSelected && App.capabilities.indexOf("dm.group") >= 0
                    iconName: "plus"
                    tip: qsTr("New group conversation")
                    onClicked: sidebar.newGroup()
                }
                IconButton {
                    visible: !App.homeSelected
                    iconName: "chevron-down"
                    tip: qsTr("Server menu")
                    onClicked: serverMenu.popup()
                }
            }
            MouseArea {
                id: headerArea
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.RightButton
                onClicked: if (!App.homeSelected) serverMenu.popup()
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
        }

        ArtworkBanner {
            id: serverBanner
            objectName: "serverBanner"
            Layout.fillWidth: true
            Layout.preferredHeight: Metrics.px(84)
            visible: !App.homeSelected && App.selectedServerBannerId.length > 0 && App.selectedServerBannerId !== "0"
            source: App.selectedServerBannerId && App.selectedServerBannerId !== "0"
                    ? (App.previews[App.selectedServerBannerId] || "") : ""
            failed: !!App.previewErrors[App.selectedServerBannerId]
            onRetry: App.requestPreview(App.selectedServerBannerId, "server-banner.png", 0)
            fillMode: Image.PreserveAspectFit
        }

        ListView {
            id: list
            objectName: "channelList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: Metrics.px(6)
            clip: true
            model: App.channels
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: true
            highlightFollowsCurrentItem: false
            activeFocusOnTab: true
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            Accessible.role: Accessible.List
            Accessible.name: App.homeSelected ? qsTr("Direct messages") : qsTr("Channels")

            Keys.onReturnPressed: activate(currentIndex)
            Keys.onEnterPressed: activate(currentIndex)
            Keys.onSpacePressed: activate(currentIndex)
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                    sidebar.openContextMenu(currentIndex, currentItem)
                    event.accepted = true
                }
            }
            function activate(i) {
                if (i < 0 || i >= count) return
                const row = model.get(i)
                if (row.rowType === "participant")
                    return
                if (row.rowType === "category") App.toggleCategory(row.itemId)
                else App.selectChannel(row.itemId)
            }

            delegate: Loader {
                id: rowLoader
                required property var model
                required property int index
                width: ListView.view.width
                sourceComponent: model.rowType === "category" ? categoryRow
                               : model.rowType === "participant" ? participantRow
                               : channelRow
                readonly property bool focused: list.activeFocus && list.currentIndex === index
            }

            // Empty states
            Text {
                anchors.centerIn: parent
                width: parent.width - Metrics.px(32)
                visible: list.count === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: Theme.textFaint
                font.pixelSize: Metrics.px(12)
                text: App.homeSelected ? qsTr("No conversations yet. Right-click a member to message them, or start a group with +.")
                                       : qsTr("No channels you can see.")
            }
        }
    }

    // ------------------------------------------------------------ row kinds
    Component {
        id: categoryRow
        Rectangle {
            objectName: "categoryRow"
            implicitHeight: Metrics.px(30)
            color: "transparent"
            border.width: parent && parent.focused ? 2 : 0
            border.color: Theme.focus
            radius: Metrics.px(4)
            readonly property var m: parent ? parent.model : null
            Component.onCompleted: if (m && m.iconAttachmentId && m.iconAttachmentId !== "0")
                                       App.requestPreview(m.iconAttachmentId, "category-icon.png", 0)
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Metrics.px(6)
                anchors.rightMargin: Metrics.px(8)
                anchors.topMargin: Metrics.px(8)
                spacing: Metrics.px(2)
                Icon {
                    name: m && m.collapsed ? "chevron-right" : "chevron-down"
                    size: Metrics.px(12)
                }
                Item {
                    visible: !!(m && m.iconAttachmentId && m.iconAttachmentId !== "0")
                    Layout.preferredWidth: Metrics.px(16)
                    Layout.preferredHeight: Metrics.px(16)
                    Icon {
                        anchors.centerIn: parent
                        name: "hash"
                        size: Metrics.px(16)
                        visible: categoryImage.status !== Image.Ready
                    }
                    Image {
                        id: categoryImage
                        anchors.fill: parent
                        source: m ? (App.previews[m.iconAttachmentId] || "") : ""
                        visible: status === Image.Ready
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                }
                SectionLabel {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: m ? m.name.toUpperCase() : ""
                    textFormat: Text.PlainText
                    color: catArea.containsMouse ? Theme.text : Theme.textMuted
                }
                IconButton {
                    visible: App.canManageChannels
                    implicitWidth: Metrics.px(20)
                    implicitHeight: Metrics.px(20)
                    iconSize: Metrics.px(14)
                    iconName: "plus"
                    tip: qsTr("Create channel")
                    onClicked: sidebar.createChannel(m.itemId)
                }
            }
            MouseArea {
                id: catArea
                anchors.fill: parent
                anchors.rightMargin: Metrics.px(30)
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: mouse => {
                    if (mouse.button === Qt.RightButton) {
                        sidebar.openContextMenu(parent.parent.index, parent)
                    } else {
                        App.toggleCategory(m.itemId)
                    }
                }
            }
            Accessible.role: Accessible.Button
            Accessible.name: (m ? m.name : "") + (m && m.collapsed ? qsTr(", collapsed") : qsTr(", expanded"))
        }
    }

    Component {
        id: channelRow
        Rectangle {
            id: row
            readonly property var m: parent ? parent.model : null
            readonly property bool isVoice: m && m.rowType === "voice"
            readonly property bool isDm: m && m.rowType === "dm"
            readonly property bool isGroup: m && m.rowType === "group_dm"
            readonly property bool emphasized: m && (m.unread || m.selected)
            readonly property string iconId: m && m.iconAttachmentId ? m.iconAttachmentId : ""
            Component.onCompleted: if (iconId.length > 0 && iconId !== "0")
                                       App.requestPreview(iconId, "channel-icon.png", 0)
            implicitHeight: isDm ? Metrics.px(40) : Metrics.px(30)
            anchors.left: parent ? parent.left : undefined
            anchors.right: parent ? parent.right : undefined
            anchors.leftMargin: Metrics.px(8) + (m ? m.depth : 0) * Metrics.px(6)
            anchors.rightMargin: Metrics.px(8)
            radius: Metrics.px(4)
            color: m && m.selected ? Theme.selection : (area.containsMouse ? Theme.raised : "transparent")
            border.width: parent && parent.focused ? 2 : 0
            border.color: Theme.focus

            Accessible.role: isVoice ? Accessible.Button : Accessible.ListItem
            Accessible.name: (isVoice ? qsTr("Voice channel %1, %n connected", "", m ? m.voiceCount : 0).arg(m ? m.name : "")
                                      : (isDm ? qsTr("Direct message with %1").arg(m ? m.name : "")
                                              : qsTr("Text channel %1").arg(m ? m.name : "")))
                             + (m && m.mentions > 0 ? qsTr(", %n mention(s)", "", m.mentions) : (m && m.unread ? qsTr(", unread") : ""))
                             + (m && m.locked ? qsTr(", locked") : "")
            Accessible.selected: m ? m.selected : false

            Rectangle {
                visible: m && m.unread && !m.selected
                anchors.right: parent.left
                anchors.rightMargin: Metrics.px(2)
                anchors.verticalCenter: parent.verticalCenter
                width: Metrics.px(4)
                height: Metrics.px(8)
                radius: Metrics.px(2)
                color: Theme.text
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Metrics.px(8)
                anchors.rightMargin: Metrics.px(6)
                spacing: Metrics.px(8)

                Avatar {
                    visible: row.isDm
                    userId: m ? m.userId : ""
                    name: m ? m.name : ""
                    status: m ? m.presence : ""
                    size: Metrics.px(26)
                }
                Icon {
                    visible: !row.isDm && !channelImage.visible
                    name: row.isGroup ? "users" : row.isVoice ? "speaker" : (m && m.locked ? "lock" : "hash")
                    size: Metrics.px(16)
                    color: row.emphasized ? Theme.text : Theme.textFaint
                }
                Image {
                    id: channelImage
                    visible: !row.isDm && row.iconId.length > 0 && row.iconId !== "0" && status === Image.Ready
                    source: App.previews[row.iconId] || ""
                    Layout.preferredWidth: Metrics.px(16)
                    Layout.preferredHeight: Metrics.px(16)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
                Text {
                    Layout.fillWidth: true
                    text: m ? m.name : ""
                    elide: Text.ElideRight
                    color: m && m.muted ? Theme.textFaint : (row.emphasized || area.containsMouse ? Theme.text : Theme.textMuted)
                    font.pixelSize: Metrics.px(14)
                    font.bold: m ? m.unread && !m.muted : false
                }
                Icon {
                    visible: m ? m.muted : false
                    name: "bell-off"
                    size: Metrics.px(13)
                    color: Theme.textFaint
                }
                Badge { count: m ? m.mentions : 0 }
                Text {
                    visible: row.isVoice && m && m.voiceCount > 0
                    text: m ? m.voiceCount : ""
                    color: Theme.textFaint
                    font.pixelSize: Metrics.px(11)
                }
            }

            MouseArea {
                id: area
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: mouse => {
                    if (mouse.button === Qt.RightButton) {
                        sidebar.openContextMenu(row.parent.index, row)
                    } else {
                        App.selectChannel(m.itemId)
                    }
                }
            }

            ToolTip.visible: area.containsMouse && m && m.topic.length > 0
            ToolTip.delay: 800
            ToolTip.text: m ? m.topic : ""
        }
    }

    Component {
        id: participantRow
        Item {
            readonly property var m: parent ? parent.model : null
            implicitHeight: Metrics.px(28)
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Metrics.px(36) + (m ? m.depth - 1 : 0) * Metrics.px(6)
                anchors.rightMargin: Metrics.px(14)
                spacing: Metrics.px(8)
                Avatar {
                    userId: m ? m.userId : ""
                    name: m ? m.name : ""
                    speaking: m ? m.speaking : false
                    size: Metrics.px(22)
                }
                Text {
                    Layout.fillWidth: true
                    text: m ? m.name : ""
                    elide: Text.ElideRight
                    color: m && m.speaking ? Theme.text : Theme.textMuted
                    font.pixelSize: Metrics.px(13)
                }
                Rectangle {
                    visible: m ? m.streaming : false
                    implicitWidth: liveLabel.implicitWidth + Metrics.px(8)
                    implicitHeight: Metrics.px(15)
                    radius: Metrics.px(3)
                    color: Theme.danger
                    Text {
                        id: liveLabel
                        anchors.centerIn: parent
                        text: qsTr("LIVE")
                        color: Theme.dangerText
                        font.pixelSize: Metrics.px(9)
                        font.bold: true
                    }
                }
                Icon { visible: m ? m.userMuted && !m.userDeafened : false; name: "mic-off"; size: Metrics.px(13); color: Theme.danger }
                Icon { visible: m ? m.userDeafened : false; name: "headphones-off"; size: Metrics.px(13); color: Theme.danger }
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: m && m.streaming && m.userId !== App.selfId ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: mouse => {
                    if (mouse.button === Qt.LeftButton) {
                        // Watching needs you in the same voice channel.
                        if (m.streaming && m.userId !== App.selfId && App.voiceChannelId === m.itemId)
                            App.watchStream(m.userId)
                        return
                    }
                    volumePopup.userId = m.userId
                    volumePopup.userName = m.name
                    volumePopup.popup()
                }
            }
            ToolTip.visible: m && m.streaming && m.userId !== App.selfId && hoverWatch.hovered
            ToolTip.delay: 500
            ToolTip.text: App.voiceChannelId === (m ? m.itemId : "") ? qsTr("Click to watch") : qsTr("Join this channel to watch")
            HoverHandler { id: hoverWatch }
            Accessible.role: Accessible.ListItem
            Accessible.name: (m ? m.name : "") + (m && m.streaming ? qsTr(", sharing their screen") : "") + (m && m.speaking ? qsTr(", speaking") : "")
                             + (m && m.userMuted ? qsTr(", muted") : "") + (m && m.userDeafened ? qsTr(", deafened") : "")
        }
    }

    // --------------------------------------------------------------- menus
    MenuPopup {
        id: serverMenu
        MenuAction { text: qsTr("Create invite link"); enabled: App.canCreateInvites; onTriggered: App.createInvite() }
        MenuAction { text: qsTr("Create channel"); enabled: App.canManageChannels; onTriggered: sidebar.createChannel("") }
        MenuAction {
            text: qsTr("Server settings")
            enabled: App.canManageRoles || App.canManageServer
            onTriggered: sidebar.openServerSettings()
        }
        MenuAction {
            text: App.isServerOwner ? qsTr("Delete server") : qsTr("Leave server")
            danger: true
            onTriggered: serverConfirm.open()
        }
    }

    ConfirmDialog {
        id: serverConfirm
        title: App.isServerOwner ? qsTr("Delete %1?").arg(App.selectedServerName) : qsTr("Leave %1?").arg(App.selectedServerName)
        message: App.isServerOwner ? qsTr("This permanently deletes every channel and message on this server for everyone.")
                                   : qsTr("You will need a new invite to come back.")
        confirmText: App.isServerOwner ? qsTr("Delete server") : qsTr("Leave")
        destructive: true
        onConfirmed: App.isServerOwner ? App.deleteServer(App.selectedServerId) : App.leaveServer(App.selectedServerId)
    }

    ChannelPlacementDialog {
        id: placementDialog
        onClosed: list.forceActiveFocus()
    }

    MenuPopup {
        id: categoryMenu
        objectName: "categoryContextMenu"
        // Restore the list before deferred menu actions open their next popup.
        onClosed: list.forceActiveFocus()
        property string channelId
        property string channelName
        MenuAction {
            text: qsTr("Category settings")
            enabled: App.canManageChannels && App.capabilities.indexOf("channel.identity.v1") >= 0
            onTriggered: Qt.callLater(() => sidebar.openChannelSettings(categoryMenu.channelId))
        }
        MenuAction {
            text: qsTr("View details")
            onTriggered: Qt.callLater(() => sidebar.openChannelDetails(categoryMenu.channelId))
        }
        MenuAction {
            text: qsTr("Place category…")
            enabled: App.canManageChannels && App.capabilities.indexOf("channel.placement.v1") >= 0
            onTriggered: Qt.callLater(() => placementDialog.openFor(categoryMenu.channelId))
        }
        MenuAction {
            text: qsTr("Move up")
            enabled: App.canManageChannels && App.capabilities.indexOf("channel.identity.v1") >= 0
            onTriggered: App.moveChannelRelative(categoryMenu.channelId, -1)
        }
        MenuAction {
            text: qsTr("Move down")
            enabled: App.canManageChannels && App.capabilities.indexOf("channel.identity.v1") >= 0
            onTriggered: App.moveChannelRelative(categoryMenu.channelId, 1)
        }
        MenuAction {
            text: qsTr("Access and permissions")
            enabled: App.ready
            onTriggered: Qt.callLater(() => sidebar.openChannelPermissions(categoryMenu.channelId, categoryMenu.channelName))
        }
        MenuAction {
            text: qsTr("Delete category")
            danger: true
            enabled: App.canManageChannels
            onTriggered: { categoryConfirm.channelId = categoryMenu.channelId; Qt.callLater(() => categoryConfirm.open()) }
        }
    }

    MenuPopup {
        id: channelMenu
        objectName: "channelContextMenu"
        // Restore the list before deferred menu actions open their next popup.
        onClosed: list.forceActiveFocus()
        property string channelId
        property string channelName
        property bool isVoice
        property bool isGroup
        property string userId
        MenuAction {
            text: channelMenu.isVoice ? qsTr("Join voice") : qsTr("Open")
            onTriggered: App.selectChannel(channelMenu.channelId)
        }
        MenuAction {
            text: qsTr("View details")
            enabled: channelMenu.userId.length === 0 && !channelMenu.isGroup
            onTriggered: Qt.callLater(() => sidebar.openChannelDetails(channelMenu.channelId))
        }
        MenuAction {
            text: App.channelMuted(channelMenu.channelId) ? qsTr("Unmute notifications") : qsTr("Mute notifications")
            enabled: !channelMenu.isVoice
            onTriggered: App.setChannelMuted(channelMenu.channelId, !App.channelMuted(channelMenu.channelId))
        }
        MenuAction {
            text: qsTr("Add people")
            enabled: channelMenu.isGroup
            onTriggered: Qt.callLater(() => sidebar.addToGroup(channelMenu.channelId))
        }
        MenuAction {
            text: qsTr("Rename conversation")
            enabled: channelMenu.isGroup
            onTriggered: Qt.callLater(() => sidebar.renameGroup(channelMenu.channelId, channelMenu.channelName))
        }
        MenuAction {
            text: qsTr("Leave conversation")
            danger: true
            enabled: channelMenu.isGroup
            onTriggered: { leaveGroupConfirm.channelId = channelMenu.channelId; Qt.callLater(() => leaveGroupConfirm.open()) }
        }
        MenuAction {
            text: qsTr("Channel settings")
            enabled: App.canManageChannels && App.capabilities.indexOf("channel.identity.v1") >= 0
                     && channelMenu.userId.length === 0 && !channelMenu.isGroup
            onTriggered: Qt.callLater(() => sidebar.openChannelSettings(channelMenu.channelId))
        }
        MenuAction {
            text: qsTr("Place channel…")
            enabled: App.canManageChannels && App.capabilities.indexOf("channel.placement.v1") >= 0
                     && channelMenu.userId.length === 0 && !channelMenu.isGroup
            onTriggered: Qt.callLater(() => placementDialog.openFor(channelMenu.channelId))
        }
        MenuAction {
            text: qsTr("Move up")
            enabled: App.canManageChannels && App.capabilities.indexOf("channel.identity.v1") >= 0
                     && channelMenu.userId.length === 0 && !channelMenu.isGroup
            onTriggered: App.moveChannelRelative(channelMenu.channelId, -1)
        }
        MenuAction {
            text: qsTr("Move down")
            enabled: App.canManageChannels && App.capabilities.indexOf("channel.identity.v1") >= 0
                     && channelMenu.userId.length === 0 && !channelMenu.isGroup
            onTriggered: App.moveChannelRelative(channelMenu.channelId, 1)
        }
        MenuAction {
            text: qsTr("Access and permissions")
            enabled: App.ready && channelMenu.userId.length === 0 && !channelMenu.isGroup
            onTriggered: Qt.callLater(() => sidebar.openChannelPermissions(channelMenu.channelId, channelMenu.channelName))
        }
        MenuAction {
            text: qsTr("Copy channel ID")
            onTriggered: App.copyText(channelMenu.channelId)
        }
        MenuAction {
            text: qsTr("Delete channel")
            danger: true
            enabled: App.canManageChannels && channelMenu.userId.length === 0
            onTriggered: { channelConfirm.channelId = channelMenu.channelId; Qt.callLater(() => channelConfirm.open()) }
        }
    }

    ConfirmDialog {
        id: leaveGroupConfirm
        property string channelId
        title: qsTr("Leave the conversation?")
        message: qsTr("You will stop receiving its messages unless someone adds you back.")
        confirmText: qsTr("Leave")
        destructive: true
        onConfirmed: App.leaveGroup(channelId)
    }

    ConfirmDialog {
        id: categoryConfirm
        objectName: "categoryDeleteConfirmation"
        property string channelId
        title: qsTr("Delete category?")
        message: qsTr("Its channels move to the end of the top-level list. Channels, messages and their own permission overrides are kept. If this category has inherited permission overrides, review and move its channels before deleting it.")
        confirmText: qsTr("Delete category")
        destructive: true
        onConfirmed: App.deleteChannel(channelId)
    }

    ConfirmDialog {
        id: channelConfirm
        property string channelId
        title: qsTr("Delete channel?")
        message: qsTr("All messages in this channel will be permanently deleted.")
        confirmText: qsTr("Delete")
        destructive: true
        onConfirmed: App.deleteChannel(channelId)
    }

    // Local per-user volume (never sent to the server).
    Popup {
        id: volumePopup
        property string userId
        property string userName
        function popup() {
            slider.value = App.userVolume(userId)
            x = (sidebar.width - width) / 2
            y = sidebar.height / 3
            open()
        }
        width: Metrics.px(220)
        padding: Metrics.px(12)
        background: Rectangle { color: Theme.raised; radius: Metrics.px(6); border.color: Theme.border }
        ColumnLayout {
            anchors.fill: parent
            spacing: Metrics.px(6)
            Text {
                text: qsTr("Volume for %1").arg(volumePopup.userName)
                color: Theme.text
                font.pixelSize: Metrics.px(12)
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            RowLayout {
                Slider {
                    id: slider
                    Layout.fillWidth: true
                    from: 0
                    to: 200
                    stepSize: 5
                    Accessible.name: qsTr("User volume")
                    onMoved: App.setUserVolume(volumePopup.userId, value)
                }
                Text {
                    text: Math.round(slider.value) + "%"
                    color: Theme.textMuted
                    font.pixelSize: Metrics.px(12)
                    Layout.preferredWidth: Metrics.px(38)
                }
            }
            FlatButton {
                text: qsTr("Send message")
                Layout.fillWidth: true
                onClicked: { volumePopup.close(); App.openDm(volumePopup.userId) }
            }
        }
    }
}
