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

    function focusList() {
        list.forceActiveFocus()
        const i = App.channels.indexOf("itemId", App.selectedChannelId)
        list.currentIndex = i >= 0 ? i : 0
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header: server name + actions
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.px(46)
            color: headerArea.containsMouse && !App.homeSelected ? Theme.raised : "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.px(14)
                anchors.rightMargin: Theme.px(6)
                Text {
                    Layout.fillWidth: true
                    text: App.selectedServerName
                    color: Theme.text
                    font.pixelSize: Theme.px(15)
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

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: Theme.px(6)
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
            Keys.onSpacePressed: activate(currentIndex)
            function activate(i) {
                const row = model.get(i)
                if (row.rowType === "participant")
                    return
                App.selectChannel(row.itemId)
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
                width: parent.width - Theme.px(32)
                visible: list.count === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: Theme.textFaint
                font.pixelSize: Theme.px(12)
                text: App.homeSelected ? qsTr("No conversations yet. Right-click a member to message them.")
                                       : qsTr("No channels you can see.")
            }
        }
    }

    // ------------------------------------------------------------ row kinds
    Component {
        id: categoryRow
        Item {
            implicitHeight: Theme.px(30)
            readonly property var m: parent ? parent.model : null
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.px(6)
                anchors.rightMargin: Theme.px(8)
                anchors.topMargin: Theme.px(8)
                spacing: Theme.px(2)
                Icon {
                    name: m && m.collapsed ? "chevron-right" : "chevron-down"
                    size: Theme.px(12)
                }
                SectionLabel {
                    Layout.fillWidth: true
                    text: m ? m.name.toUpperCase() : ""
                    color: catArea.containsMouse ? Theme.text : Theme.textMuted
                }
                IconButton {
                    visible: App.canManageChannels
                    implicitWidth: Theme.px(20)
                    implicitHeight: Theme.px(20)
                    iconSize: Theme.px(14)
                    iconName: "plus"
                    tip: qsTr("Create channel")
                    onClicked: sidebar.createChannel(m.itemId)
                }
            }
            MouseArea {
                id: catArea
                anchors.fill: parent
                anchors.rightMargin: Theme.px(30)
                hoverEnabled: true
                onClicked: App.toggleCategory(m.itemId)
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
            readonly property bool emphasized: m && (m.unread || m.selected)
            implicitHeight: isDm ? Theme.px(40) : Theme.px(30)
            anchors.left: parent ? parent.left : undefined
            anchors.right: parent ? parent.right : undefined
            anchors.leftMargin: Theme.px(8) + (m ? m.depth : 0) * Theme.px(6)
            anchors.rightMargin: Theme.px(8)
            radius: Theme.px(4)
            color: m && m.selected ? Theme.selection : (area.containsMouse ? Theme.raised : "transparent")
            border.width: parent && parent.focused ? 2 : 0
            border.color: Theme.accent

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
                anchors.rightMargin: Theme.px(2)
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.px(4)
                height: Theme.px(8)
                radius: Theme.px(2)
                color: Theme.text
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.px(8)
                anchors.rightMargin: Theme.px(6)
                spacing: Theme.px(8)

                Avatar {
                    visible: row.isDm
                    userId: m ? m.userId : ""
                    name: m ? m.name : ""
                    status: m ? m.presence : ""
                    size: Theme.px(26)
                }
                Icon {
                    visible: !row.isDm
                    name: row.isVoice ? "speaker" : (m && m.locked ? "lock" : "hash")
                    size: Theme.px(16)
                    color: row.emphasized ? Theme.text : Theme.textFaint
                }
                Text {
                    Layout.fillWidth: true
                    text: m ? m.name : ""
                    elide: Text.ElideRight
                    color: m && m.muted ? Theme.textFaint : (row.emphasized || area.containsMouse ? Theme.text : Theme.textMuted)
                    font.pixelSize: Theme.px(14)
                    font.bold: m ? m.unread && !m.muted : false
                }
                Icon {
                    visible: m ? m.muted : false
                    name: "bell-off"
                    size: Theme.px(13)
                    color: Theme.textFaint
                }
                Badge { count: m ? m.mentions : 0 }
                Text {
                    visible: row.isVoice && m && m.voiceCount > 0
                    text: m ? m.voiceCount : ""
                    color: Theme.textFaint
                    font.pixelSize: Theme.px(11)
                }
            }

            MouseArea {
                id: area
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: mouse => {
                    if (mouse.button === Qt.RightButton) {
                        channelMenu.channelId = m.itemId
                        channelMenu.isVoice = row.isVoice
                        channelMenu.userId = m.userId
                        channelMenu.popup()
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
            implicitHeight: Theme.px(28)
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.px(36) + (m ? m.depth - 1 : 0) * Theme.px(6)
                anchors.rightMargin: Theme.px(14)
                spacing: Theme.px(8)
                Avatar {
                    userId: m ? m.userId : ""
                    name: m ? m.name : ""
                    speaking: m ? m.speaking : false
                    size: Theme.px(22)
                }
                Text {
                    Layout.fillWidth: true
                    text: m ? m.name : ""
                    elide: Text.ElideRight
                    color: m && m.speaking ? Theme.text : Theme.textMuted
                    font.pixelSize: Theme.px(13)
                }
                Icon { visible: m ? m.userMuted && !m.userDeafened : false; name: "mic-off"; size: Theme.px(13); color: Theme.danger }
                Icon { visible: m ? m.userDeafened : false; name: "headphones-off"; size: Theme.px(13); color: Theme.danger }
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                onClicked: {
                    volumePopup.userId = m.userId
                    volumePopup.userName = m.name
                    volumePopup.popup()
                }
            }
            Accessible.role: Accessible.ListItem
            Accessible.name: (m ? m.name : "") + (m && m.speaking ? qsTr(", speaking") : "")
                             + (m && m.userMuted ? qsTr(", muted") : "") + (m && m.userDeafened ? qsTr(", deafened") : "")
        }
    }

    // --------------------------------------------------------------- menus
    MenuPopup {
        id: serverMenu
        MenuAction { text: qsTr("Create invite link"); enabled: App.canCreateInvites; onTriggered: App.createInvite() }
        MenuAction { text: qsTr("Create channel"); enabled: App.canManageChannels; onTriggered: sidebar.createChannel("") }
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

    MenuPopup {
        id: channelMenu
        property string channelId
        property bool isVoice
        property string userId
        MenuAction {
            text: channelMenu.isVoice ? qsTr("Join voice") : qsTr("Open")
            onTriggered: App.selectChannel(channelMenu.channelId)
        }
        MenuAction {
            text: App.channelMuted(channelMenu.channelId) ? qsTr("Unmute notifications") : qsTr("Mute notifications")
            enabled: !channelMenu.isVoice
            onTriggered: App.setChannelMuted(channelMenu.channelId, !App.channelMuted(channelMenu.channelId))
        }
        MenuAction {
            text: qsTr("Copy channel ID")
            onTriggered: App.copyText(channelMenu.channelId)
        }
        MenuAction {
            text: qsTr("Delete channel")
            danger: true
            enabled: App.canManageChannels && channelMenu.userId.length === 0
            onTriggered: { channelConfirm.channelId = channelMenu.channelId; channelConfirm.open() }
        }
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
        width: Theme.px(220)
        padding: Theme.px(12)
        background: Rectangle { color: Theme.raised; radius: Theme.px(6); border.color: Theme.border }
        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.px(6)
            Text {
                text: qsTr("Volume for %1").arg(volumePopup.userName)
                color: Theme.text
                font.pixelSize: Theme.px(12)
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
                    font.pixelSize: Theme.px(12)
                    Layout.preferredWidth: Theme.px(38)
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
