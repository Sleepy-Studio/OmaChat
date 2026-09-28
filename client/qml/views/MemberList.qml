import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// Members of the selected server (or DM participants), online first.
Rectangle {
    id: panel
    color: Theme.surface

    ListView {
        id: list
        anchors.fill: parent
        anchors.topMargin: Theme.px(8)
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
            leftPadding: Theme.px(16)
            topPadding: Theme.px(12)
            bottomPadding: Theme.px(4)
            text: section.toUpperCase()
        }

        delegate: Rectangle {
            id: row
            required property string userId
            required property string name
            required property string username
            required property string status
            required property string nameColor
            required property bool isOwner
            required property bool inVoice
            required property bool speaking
            required property int index

            width: ListView.view.width - Theme.px(12)
            x: Theme.px(6)
            height: Theme.px(40)
            radius: Theme.px(4)
            color: area.containsMouse ? Theme.raised : "transparent"
            border.width: list.activeFocus && list.currentIndex === index ? 2 : 0
            border.color: Theme.accent
            opacity: status === "offline" ? 0.55 : 1

            Accessible.role: Accessible.ListItem
            Accessible.name: name + ", " + (status === "dnd" ? qsTr("do not disturb") : status)
                             + (isOwner ? qsTr(", server owner") : "") + (inVoice ? qsTr(", in voice") : "")

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.px(8)
                anchors.rightMargin: Theme.px(8)
                spacing: Theme.px(10)
                Avatar {
                    userId: row.userId
                    name: row.name
                    status: row.status
                    speaking: row.speaking
                    size: Theme.px(30)
                }
                Text {
                    Layout.fillWidth: true
                    text: row.name
                    color: row.nameColor
                    font.pixelSize: Theme.px(14)
                    elide: Text.ElideRight
                }
                Icon { visible: row.isOwner; name: "crown"; size: Theme.px(13); color: Theme.warning }
                Icon { visible: row.inVoice; name: "speaker"; size: Theme.px(13); color: row.speaking ? Theme.success : Theme.textFaint }
            }

            MouseArea {
                id: area
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: mouse => {
                    memberMenu.userId = row.userId
                    memberMenu.userName = row.name
                    memberMenu.isOwner = row.isOwner
                    memberMenu.popup()
                }
            }
            ToolTip.visible: area.containsMouse
            ToolTip.delay: 700
            ToolTip.text: "@" + row.username
        }

        Keys.onReturnPressed: {
            const r = model.get(currentIndex)
            if (r.userId !== App.selfId)
                App.openDm(r.userId)
        }
    }

    MenuPopup {
        id: memberMenu
        property string userId
        property string userName
        property bool isOwner
        readonly property bool isSelf: userId === App.selfId
        MenuAction { text: qsTr("Message"); enabled: !memberMenu.isSelf; onTriggered: App.openDm(memberMenu.userId) }
        MenuAction { text: qsTr("Mention"); onTriggered: App.copyText("@" + App.members.get(App.members.indexOf("userId", memberMenu.userId)).username) }
        MenuAction { text: qsTr("Copy user ID"); onTriggered: App.copyText(memberMenu.userId) }
        MenuAction {
            text: qsTr("Kick %1").arg(memberMenu.userName)
            danger: true
            enabled: App.canKick && !memberMenu.isSelf && !memberMenu.isOwner
            onTriggered: { modConfirm.action = "kick"; modConfirm.open() }
        }
        MenuAction {
            text: qsTr("Ban %1").arg(memberMenu.userName)
            danger: true
            enabled: App.canBan && !memberMenu.isSelf && !memberMenu.isOwner
            onTriggered: { modConfirm.action = "ban"; modConfirm.open() }
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
