import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// Header, virtualized message history and composer for the selected channel.
Rectangle {
    id: pane
    color: Theme.background

    property bool membersVisible: true
    signal toggleMembers()
    signal openSearch()

    function focusComposer() { composer.focusInput() }

    Connections {
        target: App
        function onFocusComposer() { composer.focusInput() }
        function onSelectionChanged() { editState.messageId = "" }
    }

    QtObject {
        id: editState
        property string messageId
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---------------------------------------------------------- header
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.px(46)
            color: Theme.background
            visible: App.selectedChannelId.length > 0

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.px(16)
                anchors.rightMargin: Theme.px(8)
                spacing: Theme.px(8)

                Icon {
                    name: App.selectedChannelType === "group_dm" ? "users" : App.homeSelected ? "at" : "hash"
                    size: Theme.px(20)
                    color: Theme.textFaint
                }
                Text {
                    text: App.selectedChannelName
                    color: Theme.text
                    font.pixelSize: Theme.px(15)
                    font.bold: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                IconButton {
                    visible: App.selectedEncrypted
                    iconName: "lock"
                    iconSize: Theme.px(15)
                    iconColor: Theme.success
                    tip: qsTr("End-to-end encrypted. Compare safety numbers")
                    onClicked: safetyDialog.open()
                }
                Rectangle {
                    visible: topic.text.length > 0
                    width: 1
                    height: Theme.px(20)
                    color: Theme.border
                }
                Text {
                    id: topic
                    Layout.fillWidth: true
                    text: App.selectedChannelTopic
                    color: Theme.textMuted
                    font.pixelSize: Theme.px(13)
                    elide: Text.ElideRight
                    MouseArea {
                        anchors.fill: parent
                        enabled: App.canManageChannels
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: topicDialog.open()
                    }
                }
                Item { Layout.fillWidth: topic.text.length === 0 }
                IconButton {
                    visible: App.canManageChannels && topic.text.length === 0
                    iconName: "edit"
                    tip: qsTr("Set topic")
                    onClicked: topicDialog.open()
                }
                IconButton {
                    iconName: "search"
                    tip: qsTr("Search this channel (%1)").arg(App.shortcuts["search"] || "Ctrl+F")
                    onClicked: pane.openSearch()
                }
                IconButton {
                    iconName: "users"
                    checkable: true
                    checked: pane.membersVisible
                    iconColor: checked ? Theme.text : Theme.textMuted
                    tip: qsTr("Member list")
                    onClicked: pane.toggleMembers()
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
        }

        // -------------------------------------------------------- messages
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: messages
                anchors.fill: parent
                visible: App.selectedChannelId.length > 0
                model: App.messages
                verticalLayoutDirection: ListView.BottomToTop
                clip: true
                reuseItems: true
                cacheBuffer: Theme.px(600)
                boundsBehavior: Flickable.StopAtBounds
                activeFocusOnTab: true
                keyNavigationEnabled: true
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Messages in %1").arg(App.selectedChannelName)

                // One shared menu/dialog instead of one per delegate.
                function openMenu(target) {
                    messageMenu.target = target
                    messageMenu.popup()
                }
                function confirmDelete(id) {
                    deleteConfirm.messageId = id
                    deleteConfirm.open()
                }

                delegate: MessageDelegate {
                    width: ListView.view.width
                    editing: editState.messageId === messageId
                    onEditRequested: id => {
                        editState.messageId = id
                        composer.beginEdit(id, content)
                    }
                }

                header: Item { width: 1; height: Theme.px(10) }

                footer: Item {
                    width: messages.width
                    height: App.messages.hasMore || App.messages.loading ? Theme.px(40)
                          : (App.messages.count > 0 ? welcome.implicitHeight + Theme.px(32) : 0)
                    BusyIndicator {
                        anchors.centerIn: parent
                        running: App.messages.loading
                        visible: running
                        implicitWidth: Theme.px(24)
                        implicitHeight: Theme.px(24)
                    }
                    Column {
                        id: welcome
                        visible: !App.messages.hasMore && !App.messages.loading && App.messages.count > 0
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.px(16)
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: Theme.px(8)
                        spacing: Theme.px(4)
                        Text {
                            text: App.homeSelected ? App.selectedChannelName : qsTr("Welcome to #%1").arg(App.selectedChannelName)
                            color: Theme.text
                            font.pixelSize: Theme.px(20)
                            font.bold: true
                        }
                        Text {
                            text: App.homeSelected ? qsTr("This is the beginning of your direct messages.")
                                                   : qsTr("This is the start of the channel.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.px(13)
                        }
                    }
                }

                // Content coordinates run oldest (top) to newest (bottom), so
                // the latest message is in view when the visible area ends at 1.
                readonly property bool nearLatest: visibleArea.yPosition + visibleArea.heightRatio > 0.985
                property bool pinned: true
                onMovementEnded: pinned = nearLatest
                onCountChanged: if (pinned) positionViewAtBeginning()
            }

            EmptyState {
                anchors.centerIn: parent
                visible: App.selectedChannelId.length === 0
                icon: App.homeSelected ? "message" : "hash"
                title: App.homeSelected ? qsTr("No direct messages") : qsTr("No channel selected")
                subtitle: App.homeSelected ? qsTr("Right-click someone in a server's member list and choose “Message”.")
                                           : qsTr("Pick a channel on the left, or press %1 to jump anywhere.").arg(App.shortcuts["quick_switcher"] || "Ctrl+K")
            }

            EmptyState {
                anchors.centerIn: parent
                visible: App.selectedChannelId.length > 0 && App.messages.count === 0 && !App.messages.loading
                icon: App.messages.error.length > 0 ? "alert" : "message"
                title: App.messages.error.length > 0 ? qsTr("Cannot load messages") : qsTr("No messages yet")
                subtitle: App.messages.error.length > 0 ? App.messages.error
                        : (App.canSend ? qsTr("Say hello — the first message starts the conversation.")
                                       : qsTr("You can read this channel but not post in it."))
            }

            // Jump to latest
            FlatButton {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: Theme.px(12)
                visible: messages.visible && !messages.nearLatest && messages.contentHeight > messages.height * 1.5
                text: qsTr("Jump to latest")
                onClicked: {
                    messages.positionViewAtBeginning()
                    messages.pinned = true
                }
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.px(16)
            Layout.preferredHeight: Theme.px(18)
            text: App.typingText
            color: Theme.textMuted
            font.pixelSize: Theme.px(12)
            font.italic: true
            elide: Text.ElideRight
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        Composer {
            id: composer
            Layout.fillWidth: true
            visible: App.selectedChannelId.length > 0
            editingId: editState.messageId
            onEditFinished: editState.messageId = ""
            onEditLastRequested: {
                const last = App.messages.lastOwnMessage()
                if (last.id !== undefined) {
                    editState.messageId = last.id
                    composer.beginEdit(last.id, last.content)
                }
            }
        }
    }

    MenuPopup {
        id: messageMenu
        property var target: null
        MenuAction { text: qsTr("Reply"); enabled: App.canSend; onTriggered: App.startReply(messageMenu.target.messageId) }
        MenuAction { text: qsTr("Add 👍"); onTriggered: App.toggleReaction(messageMenu.target.messageId, "👍") }
        MenuAction { text: qsTr("Add ❤️"); onTriggered: App.toggleReaction(messageMenu.target.messageId, "❤️") }
        MenuAction { text: qsTr("Add 😂"); onTriggered: App.toggleReaction(messageMenu.target.messageId, "😂") }
        MenuAction { text: qsTr("Copy text"); onTriggered: App.copyText(messageMenu.target.content) }
        MenuAction { text: qsTr("Copy message ID"); onTriggered: App.copyText(messageMenu.target.messageId) }
        MenuAction {
            text: qsTr("Edit")
            enabled: messageMenu.target !== null && messageMenu.target.isOwn
            onTriggered: {
                editState.messageId = messageMenu.target.messageId
                composer.beginEdit(messageMenu.target.messageId, messageMenu.target.content)
            }
        }
        MenuAction {
            text: qsTr("Delete message")
            danger: true
            enabled: messageMenu.target !== null && messageMenu.target.canDelete
            onTriggered: messages.confirmDelete(messageMenu.target.messageId)
        }
    }

    ConfirmDialog {
        id: deleteConfirm
        property string messageId
        title: qsTr("Delete message?")
        message: qsTr("This cannot be undone.")
        confirmText: qsTr("Delete")
        destructive: true
        onConfirmed: App.deleteMessage(messageId)
    }

    TextPromptDialog {
        id: topicDialog
        title: qsTr("Channel topic")
        label: qsTr("Topic for #%1").arg(App.selectedChannelName)
        acceptText: qsTr("Save")
        allowEmpty: true
        onAboutToShow: value = App.selectedChannelTopic
        onAccepted: value => App.setTopic(value)
    }

    SafetyDialog { id: safetyDialog }
}
