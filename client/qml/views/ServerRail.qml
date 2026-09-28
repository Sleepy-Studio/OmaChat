import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// Vertical server list: direct messages first, then servers.
Rectangle {
    id: rail
    color: Theme.surfaceAlt

    signal createServer()
    signal joinServer()

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Theme.px(8)
        anchors.bottomMargin: Theme.px(8)
        spacing: Theme.px(6)

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: App.servers
            spacing: Theme.px(6)
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: true
            activeFocusOnTab: true
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Servers")

            delegate: Item {
                id: entry
                required property string itemId
                required property string name
                required property string initials
                required property bool isHome
                required property bool unread
                required property int mentions
                required property bool selected
                required property bool inVoice
                required property int index

                width: ListView.view.width
                height: Theme.px(44)

                Accessible.role: Accessible.PageTab
                Accessible.name: name + (mentions > 0 ? qsTr(", %n mention(s)", "", mentions) : unread ? qsTr(", unread") : "")
                Accessible.selected: selected

                // Selection / unread pill on the left edge.
                Rectangle {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.px(4)
                    height: entry.selected ? Theme.px(32) : (entry.unread ? Theme.px(8) : 0)
                    radius: width / 2
                    color: Theme.text
                    Behavior on height { NumberAnimation { duration: Theme.animationMs } }
                }

                Rectangle {
                    id: tile
                    anchors.centerIn: parent
                    width: Theme.px(42)
                    height: Theme.px(42)
                    radius: entry.selected || area.containsMouse ? Theme.px(12) : width / 2
                    color: entry.selected ? Theme.accent : (area.containsMouse ? Theme.raised : Theme.surface)
                    border.width: list.activeFocus && list.currentIndex === entry.index ? 2 : 0
                    border.color: Theme.text
                    Behavior on radius { NumberAnimation { duration: Theme.animationMs } }

                    Icon {
                        anchors.centerIn: parent
                        visible: entry.isHome
                        name: "message"
                        size: Theme.px(20)
                        color: entry.selected ? Theme.accentText : Theme.text
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: !entry.isHome
                        text: entry.initials
                        color: entry.selected ? Theme.accentText : Theme.text
                        font.pixelSize: Theme.px(15)
                        font.bold: true
                    }
                    Icon {
                        visible: entry.inVoice
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: -Theme.px(2)
                        name: "speaker"
                        size: Theme.px(13)
                        color: Theme.success
                    }
                }

                Badge {
                    count: entry.mentions
                    anchors.right: tile.right
                    anchors.bottom: tile.bottom
                    anchors.rightMargin: -Theme.px(4)
                    anchors.bottomMargin: -Theme.px(2)
                }

                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: mouse => {
                        if (mouse.button === Qt.RightButton && !entry.isHome) {
                            menu.serverId = entry.itemId
                            menu.popup()
                        } else {
                            App.selectServer(entry.itemId)
                        }
                    }
                }

                ToolTip.visible: area.containsMouse
                ToolTip.delay: 400
                ToolTip.text: entry.name
            }

            Keys.onReturnPressed: App.selectServer(model.get(currentIndex).itemId)
            Keys.onSpacePressed: App.selectServer(model.get(currentIndex).itemId)
        }

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: Theme.px(30)
            implicitHeight: 1
            color: Theme.border
        }

        IconButton {
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: Theme.px(42)
            implicitHeight: Theme.px(42)
            iconName: "plus"
            iconColor: Theme.success
            tip: qsTr("Add a server")
            onClicked: addMenu.popup()
        }

        // Accounts: every saved account stays connected; this picks the one shown.
        Item {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: Theme.px(8)
            implicitWidth: Theme.px(42)
            implicitHeight: Theme.px(42)
            Avatar {
                anchors.centerIn: parent
                userId: App.selfId
                name: App.selfName
                size: Theme.px(34)
            }
            Badge {
                anchors.right: parent.right
                anchors.top: parent.top
                count: App.backgroundUnread
            }
            MouseArea {
                id: accountArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: accountMenu.popup()
            }
            ToolTip.visible: accountArea.containsMouse
            ToolTip.delay: 400
            ToolTip.text: qsTr("Accounts")
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Accounts, %n mention(s) elsewhere", "", App.backgroundUnread)
        }
    }

    MenuPopup {
        id: accountMenu
        Instantiator {
            model: App.accounts
            delegate: MenuAction {
                required property var modelData
                text: (modelData.active ? "✓ " : "") + modelData.username + "@" + modelData.host
                      + (modelData.state !== "connected" ? " — " + modelData.state.replace("_", " ") : "")
                      + (modelData.mentions > 0 ? "  (" + modelData.mentions + ")"
                         : modelData.unread > 0 ? "  •" : "")
                enabled: !modelData.active
                onTriggered: App.switchAccount(modelData.id)
            }
            onObjectAdded: (index, object) => accountMenu.insertAction(index, object)
            onObjectRemoved: (index, object) => accountMenu.removeAction(object)
        }
        MenuSeparator {}
        MenuAction { text: qsTr("Add another account"); onTriggered: App.addingAccount = true }
    }

    MenuPopup {
        id: addMenu
        MenuAction { text: qsTr("Create a server"); onTriggered: rail.createServer() }
        MenuAction { text: qsTr("Join with an invite"); onTriggered: rail.joinServer() }
    }

    MenuPopup {
        id: menu
        property string serverId
        MenuAction {
            text: qsTr("Create invite link")
            onTriggered: { App.selectServer(menu.serverId); App.createInvite() }
        }
        MenuAction {
            text: qsTr("Leave server")
            danger: true
            onTriggered: { leaveConfirm.serverId = menu.serverId; leaveConfirm.open() }
        }
    }

    ConfirmDialog {
        id: leaveConfirm
        property string serverId
        title: qsTr("Leave server?")
        message: qsTr("You will need a new invite to come back. Owners must delete the server instead.")
        confirmText: qsTr("Leave")
        destructive: true
        onConfirmed: App.leaveServer(serverId)
    }
}
