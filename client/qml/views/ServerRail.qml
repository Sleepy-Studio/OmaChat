import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts
import OmaChat

// Vertical server list: direct messages first, then servers.
Rectangle {
    id: rail
    color: Theme.surfaceAlt

    signal createServer()
    signal joinServer()
    signal openInstanceConsole()

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Metrics.px(8)
        anchors.bottomMargin: Metrics.px(8)
        spacing: Metrics.px(6)

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: App.servers
            spacing: Metrics.px(6)
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
                required property string iconAttachmentId
                readonly property string iconId: iconAttachmentId
                Component.onCompleted: {
                    if (!isHome && iconId && iconId !== "0")
                        App.requestPreview(iconId, "server-icon.png", 0)
                }
                onIconIdChanged: {
                    if (!isHome && iconId && iconId !== "0")
                        App.requestPreview(iconId, "server-icon.png", 0)
                }

                width: ListView.view.width
                height: Metrics.px(44)

                Accessible.role: Accessible.PageTab
                Accessible.name: name + (mentions > 0 ? qsTr(", %n mention(s)", "", mentions) : unread ? qsTr(", unread") : "")
                Accessible.selected: selected

                // Selection / unread pill on the left edge.
                Rectangle {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: Metrics.px(4)
                    height: entry.selected ? Metrics.px(32) : (entry.unread ? Metrics.px(8) : 0)
                    radius: width / 2
                    color: Theme.text
                    Behavior on height { NumberAnimation { duration: Theme.animationMs } }
                }

                Rectangle {
                    id: tile
                    anchors.centerIn: parent
                    width: Metrics.px(42)
                    height: Metrics.px(42)
                    radius: entry.selected || area.containsMouse ? Metrics.px(12) : width / 2
                    color: entry.selected ? Theme.accent : (area.containsMouse ? Theme.raised : Theme.surface)
                    border.width: list.activeFocus && list.currentIndex === entry.index ? 2 : 0
                    border.color: Theme.text
                    clip: true
                    Behavior on radius { NumberAnimation { duration: Theme.animationMs } }

                    Icon {
                        anchors.centerIn: parent
                        visible: entry.isHome
                        name: "message"
                        size: Metrics.px(20)
                        color: entry.selected ? Theme.accentText : Theme.text
                    }
                    Rectangle {
                        id: iconMask
                        anchors.fill: parent
                        radius: tile.radius
                        color: "white"
                    }
                    ShaderEffectSource {
                        id: iconMaskTexture
                        sourceItem: iconMask
                        hideSource: true
                        visible: false
                    }
                    Image {
                        id: serverIcon
                        anchors.fill: parent
                        visible: !entry.isHome && entry.iconId && entry.iconId !== "0"
                                 && source.toString().length > 0 && status === Image.Ready
                        source: entry.iconId && entry.iconId !== "0" ? (App.previews[entry.iconId] || "") : ""
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        layer.enabled: true
                        layer.effect: MultiEffect {
                            maskEnabled: true
                            maskSource: iconMaskTexture
                        }
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: !entry.isHome && !serverIcon.visible
                        text: entry.initials
                        color: entry.selected ? Theme.accentText : Theme.text
                        font.pixelSize: Metrics.px(15)
                        font.bold: true
                    }
                    Icon {
                        visible: entry.inVoice
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: -Metrics.px(2)
                        name: "speaker"
                        size: Metrics.px(13)
                        color: Theme.success
                    }
                }

                Badge {
                    count: entry.mentions
                    anchors.right: tile.right
                    anchors.bottom: tile.bottom
                    anchors.rightMargin: -Metrics.px(4)
                    anchors.bottomMargin: -Metrics.px(2)
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
            implicitWidth: Metrics.px(30)
            implicitHeight: 1
            color: Theme.border
        }

        IconButton {
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: Metrics.px(42)
            implicitHeight: Metrics.px(42)
            iconName: "plus"
            iconColor: Theme.success
            tip: qsTr("Add a server")
            onClicked: addMenu.popup()
        }

        // Accounts: every saved account stays connected; this picks the one shown.
        Item {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: Metrics.px(8)
            implicitWidth: Metrics.px(42)
            implicitHeight: Metrics.px(42)
            Avatar {
                anchors.centerIn: parent
                userId: App.selfId
                name: App.selfName
                size: Metrics.px(34)
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
        Instantiator {
            model: App.isInstanceOperator ? 1 : 0
            delegate: MenuAction {
                text: qsTr("Instance console")
                onTriggered: rail.openInstanceConsole()
            }
            onObjectAdded: (index, object) => accountMenu.insertAction(App.accounts.length + 1, object)
            onObjectRemoved: (index, object) => accountMenu.removeAction(object)
        }
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
