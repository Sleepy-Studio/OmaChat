import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// Members of the selected server (or DM participants), online first.
Rectangle {
    id: panel
    color: Theme.surface
    function focusList() { list.forceActiveFocus(); if (list.currentIndex < 0 && list.count > 0) list.currentIndex = 0 }
    function openProfile() {
        if (list.currentIndex < 0 || list.currentIndex >= list.count) return
        profileDialog.userId = list.model.get(list.currentIndex).userId
        profileDialog.open()
    }
    function openMemberMenu(index, item) {
        if (index < 0 || index >= list.count) return
        const member = list.model.get(index)
        list.currentIndex = index
        memberMenu.userId = member.userId
        memberMenu.userName = member.name
        memberMenu.isOwner = member.isOwner
        if (item) {
            const point = item.mapToItem(panel, 0, item.height)
            memberMenu.popup(panel, point.x, point.y)
        } else {
            memberMenu.popup()
        }
    }

    ListView {
        id: list
        objectName: "memberList"
        anchors.fill: parent
        anchors.topMargin: Metrics.px(8)
        model: App.members
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        activeFocusOnTab: true
        keyNavigationEnabled: true
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Members")

        section.property: "section"
        section.delegate: SectionLabel {
            required property string section
            width: ListView.view.width
            leftPadding: Metrics.px(16)
            topPadding: Metrics.px(12)
            bottomPadding: Metrics.px(4)
            text: section.toUpperCase()
        }

        delegate: Rectangle {
            id: row
            required property string userId
            required property string name
            required property string username
            required property string avatarUrl
            required property string status
            required property string nameColor
            required property bool isOwner
            required property bool inVoice
            required property bool speaking
            required property int index

            width: ListView.view.width - Metrics.px(12)
            x: Metrics.px(6)
            height: Metrics.px(40)
            radius: Metrics.px(4)
            color: area.containsMouse ? Theme.raised : "transparent"
            border.width: list.activeFocus && list.currentIndex === index ? 2 : 0
            border.color: Theme.focus

            Accessible.role: Accessible.ListItem
            Accessible.name: name + ", " + (status === "dnd" ? qsTr("do not disturb") : status)
                             + (isOwner ? qsTr(", server owner") : "") + (inVoice ? qsTr(", in voice") : "")

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Metrics.px(8)
                anchors.rightMargin: Metrics.px(8)
                spacing: Metrics.px(10)
                Avatar {
                    userId: row.userId
                    name: row.name
                    avatarUrl: row.avatarUrl
                    status: row.status
                    speaking: row.speaking
                    size: Metrics.px(30)
                }
                Text {
                    Layout.fillWidth: true
                    text: row.name
                    color: row.nameColor
                    font.pixelSize: Metrics.px(14)
                    elide: Text.ElideRight
                }
                Icon { visible: row.isOwner; name: "crown"; size: Metrics.px(13); color: Theme.warning }
                Icon { visible: row.inVoice; name: "speaker"; size: Metrics.px(13); color: row.speaking ? Theme.success : Theme.textFaint }
            }

            MouseArea {
                id: area
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: mouse => {
                    panel.openMemberMenu(row.index, row)
                }
            }
            ToolTip.visible: area.containsMouse
            ToolTip.delay: 700
            ToolTip.text: "@" + row.username
        }

        Keys.onReturnPressed: panel.openProfile()
        Keys.onEnterPressed: panel.openProfile()
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                panel.openMemberMenu(currentIndex, currentItem)
                event.accepted = true
            }
        }
    }

    MenuPopup {
        id: memberMenu
        objectName: "memberContextMenu"
        // Restore the list before deferred actions open profiles or confirmations.
        onClosed: panel.focusList()
        property string userId
        property string userName
        property bool isOwner
        readonly property bool isSelf: userId === App.selfId
        MenuAction { text: qsTr("View profile"); onTriggered: { profileDialog.userId = memberMenu.userId; Qt.callLater(() => profileDialog.open()) } }
        MenuAction { text: qsTr("Message"); enabled: !memberMenu.isSelf; onTriggered: App.openDm(memberMenu.userId) }
        MenuAction {
            text: qsTr("Mention")
            onTriggered: {
                const index = App.members.indexOf("userId", memberMenu.userId)
                if (index >= 0) App.copyText("@" + App.members.get(index).username)
            }
        }
        MenuAction { text: qsTr("Copy user ID"); onTriggered: App.copyText(memberMenu.userId) }
        MenuAction {
            text: qsTr("Kick %1").arg(memberMenu.userName)
            danger: true
            enabled: App.canKick && !memberMenu.isSelf && !memberMenu.isOwner
            onTriggered: { modConfirm.action = "kick"; Qt.callLater(() => modConfirm.open()) }
        }
        MenuAction {
            text: qsTr("Ban %1").arg(memberMenu.userName)
            danger: true
            enabled: App.canBan && !memberMenu.isSelf && !memberMenu.isOwner
            onTriggered: { modConfirm.action = "ban"; Qt.callLater(() => modConfirm.open()) }
        }
    }

    Dialog {
        id: profileDialog
        objectName: "memberProfile"
        onClosed: panel.focusList()
        property string userId
        readonly property var profile: {
            App.profilesRevision
            return App.userProfile(userId)
        }
        title: qsTr("Profile")
        width: Math.min(Metrics.px(380), (parent ? parent.width : 420) - Metrics.px(32))
        height: Math.min(implicitHeight, Math.max(0, (parent ? parent.height : 600) - Metrics.px(32)))
        contentItem: ColumnLayout {
            spacing: Metrics.px(12)
            RowLayout {
                Layout.fillWidth: true
                Text { text: profileDialog.title; color: Theme.text; font.bold: true; font.pixelSize: Metrics.px(16); Layout.fillWidth: true }
                IconButton { iconName: "x"; tip: qsTr("Close"); onClicked: profileDialog.close() }
            }
            ScrollView {
                objectName: "memberProfileScroll"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 0
                Layout.preferredHeight: profileContent.implicitHeight
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    id: profileContent
                    width: parent.width
                    spacing: Metrics.px(12)
                    Avatar {
                        Layout.alignment: Qt.AlignHCenter
                        userId: profileDialog.userId
                        name: profileDialog.profile.display_name || ""
                        avatarUrl: profileDialog.profile.avatar_url || ""
                        size: Metrics.px(72)
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: profileDialog.profile.display_name || ""
                        textFormat: Text.PlainText
                        wrapMode: Text.WrapAnywhere
                        color: Theme.text
                        font.bold: true
                        font.pixelSize: Metrics.px(17)
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: "@" + (profileDialog.profile.username || "")
                        textFormat: Text.PlainText
                        wrapMode: Text.WrapAnywhere
                        color: Theme.textMuted
                        font.pixelSize: Metrics.px(12)
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: profileDialog.profile.status === "dnd" ? qsTr("Do not disturb")
                            : profileDialog.profile.status === "idle" ? qsTr("Idle")
                            : profileDialog.profile.status === "online" ? qsTr("Online") : qsTr("Offline")
                        color: Theme.textMuted
                        font.pixelSize: Metrics.px(12)
                    }
                    Text {
                        objectName: "memberProfileBio"
                        Layout.fillWidth: true
                        text: profileDialog.profile.bio || qsTr("No bio yet")
                        color: profileDialog.profile.bio ? Theme.text : Theme.textMuted
                        wrapMode: Text.WrapAnywhere
                        textFormat: Text.PlainText
                        font.pixelSize: Metrics.px(13)
                    }
                }
            }
            FlatButton {
                Layout.alignment: Qt.AlignHCenter
                visible: profileDialog.userId !== App.selfId
                text: qsTr("Message")
                onClicked: { profileDialog.close(); App.openDm(profileDialog.userId) }
            }
        }
    }

    ConfirmDialog {
        id: modConfirm
        property string action
        title: action === "ban" ? qsTr("Ban %1?").arg(memberMenu.userName) : qsTr("Kick %1?").arg(memberMenu.userName)
        message: action === "ban" ? qsTr("They are removed and cannot rejoin with an invite until unbanned.")
                                  : qsTr("They are removed but can rejoin with a new invite.")
        confirmText: action === "ban" ? qsTr("Ban") : qsTr("Kick")
        destructive: true
        onConfirmed: action === "ban" ? App.ban(memberMenu.userId, "") : App.kick(memberMenu.userId, "")
    }
}
