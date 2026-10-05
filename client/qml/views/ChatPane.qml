import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// Header, virtualized message history and composer for the selected channel.
Rectangle {
    id: pane
    color: Theme.background

    property bool membersVisible: true
    property bool showChannelsButton: false
    signal toggleMembers()
    signal toggleChannels()
    signal browseChannels()
    signal openSearch()
    signal openChannelDetails(string channelId)

    function focusComposer() { composer.focusInput() }
    function loadBanner() {
        if (App.selectedChannelBannerId.length > 0 && App.selectedChannelBannerId !== "0")
            App.requestPreview(App.selectedChannelBannerId, "channel-banner.png", 0)
    }

    Connections {
        target: App
        function onFocusComposer() { composer.focusInput() }
        function onSelectionChanged() { editState.messageId = ""; pane.loadBanner() }
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
            implicitHeight: Metrics.px(46)
            color: Theme.background
            visible: App.selectedChannelId.length > 0

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Metrics.px(16)
                anchors.rightMargin: Metrics.px(8)
                spacing: Metrics.px(8)

                IconButton {
                    visible: pane.showChannelsButton
                    iconName: "server"
                    tip: qsTr("Browse channels (%1)").arg(App.shortcuts["focus_channels"] || "Ctrl+L")
                    onClicked: pane.toggleChannels()
                }

                Icon {
                    name: App.selectedChannelType === "group_dm" ? "users" : App.homeSelected ? "at" : "hash"
                    size: Metrics.px(20)
                    color: Theme.textFaint
                }
                Text {
                    objectName: "conversationChannelName"
                    Layout.maximumWidth: parent.width * 0.35
                    Layout.minimumWidth: 0
                    text: App.selectedChannelName
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.text
                    font.pixelSize: Metrics.px(15)
                    font.bold: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                IconButton {
                    visible: App.selectedEncrypted
                    iconName: "lock"
                    iconSize: Metrics.px(15)
                    iconColor: Theme.success
                    tip: qsTr("End-to-end encrypted. Compare safety numbers")
                    onClicked: safetyDialog.open()
                }
                Rectangle {
                    visible: topic.text.length > 0
                    width: 1
                    height: Metrics.px(20)
                    color: Theme.border
                }
                Text {
                    id: topic
                    objectName: "channelTopic"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: App.selectedChannelTopic
                    textFormat: Text.PlainText
                    color: Theme.textMuted
                    font.pixelSize: Metrics.px(13)
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
                    objectName: "editChannelTopic"
                    visible: App.canManageChannels
                    iconName: "edit"
                    tip: topic.text.length === 0 ? qsTr("Set topic") : qsTr("Edit topic")
                    onClicked: topicDialog.open()
                }
                IconButton {
                    visible: !App.homeSelected && App.selectedChannelId.length > 0
                    iconName: "info"
                    tip: qsTr("Channel details")
                    onClicked: pane.openChannelDetails(App.selectedChannelId)
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

        ArtworkBanner {
            objectName: "channelBanner"
            Layout.fillWidth: true
            Layout.preferredHeight: Metrics.px(88)
            visible: App.selectedChannelBannerId.length > 0 && App.selectedChannelBannerId !== "0"
            source: App.previews[App.selectedChannelBannerId] || ""
            failed: !!App.previewErrors[App.selectedChannelBannerId]
            onRetry: pane.loadBanner()
            fillMode: Image.PreserveAspectCrop
        }

        // A persistent reminder that outlives navigating away from the
        // stream panel or collapsing it — easy to forget you're still
        // sharing after alt-tabbing to another app.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Metrics.px(32)
            visible: App.sharingScreen
            color: Theme.accent

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Metrics.px(16)
                anchors.rightMargin: Metrics.px(8)
                spacing: Metrics.px(8)

                Icon {
                    name: "monitor"
                    size: Metrics.px(15)
                    color: Theme.accentText
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("You are sharing your screen")
                    color: Theme.accentText
                    font.pixelSize: Metrics.px(12)
                    font.bold: true
                    elide: Text.ElideRight
                }
                FlatButton {
                    text: qsTr("Stop sharing")
                    onClicked: App.toggleScreenShare()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Metrics.px(36)
            visible: App.messages.anchorMessageId.length > 0
            color: Theme.surface
            border.color: Theme.border
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Metrics.px(16)
                anchors.rightMargin: Metrics.px(8)
                Text {
                    Layout.fillWidth: true
                    text: App.messages.error.length > 0
                          ? qsTr("Could not load earlier messages: %1").arg(App.messages.error)
                          : qsTr("Viewing search match and earlier messages")
                    color: Theme.text
                    font.pixelSize: Metrics.px(12)
                    elide: Text.ElideRight
                }
                FlatButton {
                    visible: App.messages.error.length > 0
                    text: qsTr("Retry")
                    onClicked: App.messages.retry()
                }
                FlatButton {
                    text: qsTr("Return to latest")
                    onClicked: App.messages.reload()
                }
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
                cacheBuffer: Metrics.px(600)
                boundsBehavior: Flickable.StopAtBounds
                activeFocusOnTab: true
                keyNavigationEnabled: true
                Keys.onReturnPressed: if (currentItem) openMenu(currentItem, true)
                Keys.onEnterPressed: if (currentItem) openMenu(currentItem, true)
                Keys.onSpacePressed: if (currentItem) openMenu(currentItem, true)
                Keys.onMenuPressed: if (currentItem) openMenu(currentItem, true)
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Messages in %1").arg(App.selectedChannelName)

                // Read receipts follow actual foreground visibility, never history fetch.
                function scheduleRead() { visibleRead.restart() }
                onContentYChanged: scheduleRead()
                onVisibleChanged: scheduleRead()
                Timer {
                    id: visibleRead
                    interval: 300
                    onTriggered: {
                        const newest = messages.itemAtIndex(0)
                        if (!newest || !messages.visible) return
                        const p = newest.mapToItem(messages, 0, 0)
                        if (p.y < messages.height && p.y + newest.height > 0)
                            App.markConversationRead(newest.messageId)
                    }
                }
                Connections {
                    target: App.messages
                    function onCountChanged() { messages.scheduleRead() }
                }

                // One shared menu/dialog instead of one per delegate.
                function openMenu(target, keyboard = false) {
                    messageMenu.target = target
                    if (keyboard) {
                        const position = target.mapToItem(pane, Metrics.px(46), 0)
                        messageMenu.x = Math.max(Metrics.px(8), Math.min(position.x, pane.width - Metrics.px(208)))
                        messageMenu.y = Math.max(Metrics.px(8), Math.min(position.y, pane.height - Metrics.px(280)))
                        messageMenu.open()
                    } else {
                        messageMenu.popup()
                    }
                }
                function confirmDelete(id) {
                    deleteConfirm.messageId = id
                    deleteConfirm.open()
                }

                delegate: MessageDelegate {
                    width: ListView.view.width
                    editing: editState.messageId === messageId
                    searchMatch: App.messages.anchorMessageId === messageId
                    onEditRequested: id => {
                        editState.messageId = id
                        composer.beginEdit(id, content)
                    }
                }

                header: Item { width: 1; height: Metrics.px(10) }

                footer: Item {
                    width: messages.width
                    height: App.messages.hasMore || App.messages.loading ? Metrics.px(40)
                          : (App.messages.count > 0 ? welcome.implicitHeight + Metrics.px(32) : 0)
                    BusyIndicator {
                        anchors.centerIn: parent
                        running: App.messages.loading
                        visible: running
                        implicitWidth: Metrics.px(24)
                        implicitHeight: Metrics.px(24)
                    }
                    Column {
                        id: welcome
                        visible: !App.messages.hasMore && !App.messages.loading && App.messages.count > 0
                        anchors.left: parent.left
                        anchors.leftMargin: Metrics.px(16)
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: Metrics.px(8)
                        spacing: Metrics.px(4)
                        Text {
                            text: App.homeSelected ? App.selectedChannelName : qsTr("Welcome to #%1").arg(App.selectedChannelName)
                            color: Theme.text
                            font.pixelSize: Metrics.px(20)
                            font.bold: true
                        }
                        Text {
                            text: App.homeSelected ? qsTr("This is the beginning of your direct messages.")
                                                   : qsTr("This is the start of the channel.")
                            color: Theme.textMuted
                            font.pixelSize: Metrics.px(13)
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
                actionText: App.homeSelected ? qsTr("Open a server") : qsTr("Browse channels")
                onActionRequested: pane.browseChannels()
            }

            EmptyState {
                anchors.centerIn: parent
                visible: App.selectedChannelId.length > 0 && App.messages.count === 0 && !App.messages.loading
                icon: App.messages.error.length > 0 ? "alert" : "message"
                title: App.messages.error.length > 0 ? qsTr("Cannot load messages") : qsTr("No messages yet")
                subtitle: App.messages.error.length > 0 ? App.messages.error
                        : (App.canSend ? qsTr("Say hello — the first message starts the conversation.")
                                       : qsTr("You can read this channel but not post in it."))
                actionText: App.messages.error.length > 0 ? qsTr("Retry")
                          : App.canSend ? qsTr("Write a message") : ""
                onActionRequested: {
                    if (App.messages.error.length > 0)
                        App.messages.retry()
                    else
                        composer.focusInput()
                }
            }

            // Jump to latest
            FlatButton {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: Metrics.px(12)
                visible: messages.visible && App.messages.anchorMessageId.length === 0
                         && !messages.nearLatest && messages.contentHeight > messages.height * 1.5
                text: qsTr("Jump to latest")
                onClicked: {
                    messages.positionViewAtBeginning()
                    messages.pinned = true
                }
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: Metrics.px(16)
            Layout.preferredHeight: Metrics.px(18)
            text: App.typingText
            color: Theme.textMuted
            font.pixelSize: Metrics.px(12)
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
