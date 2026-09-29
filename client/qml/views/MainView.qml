import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// Dense four-pane desktop layout with a full-width status bar.
Item {
    id: root

    property bool showMembers: width > Theme.px(980)

    // Application-focused push-to-talk: key events bubble up here from any
    // focused child that does not consume them (the composer ignores F-keys).
    Keys.onPressed: event => {
        if (event.key === App.pushToTalkKey && !event.isAutoRepeat && App.inputMode === "ptt") {
            App.pushToTalk(true)
            event.accepted = true
        }
    }
    Keys.onReleased: event => {
        if (event.key === App.pushToTalkKey && !event.isAutoRepeat && App.inputMode === "ptt") {
            App.pushToTalk(false)
            event.accepted = true
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        StatusBanner { Layout.fillWidth: true }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            ServerRail {
                id: rail
                Layout.fillHeight: true
                Layout.preferredWidth: Theme.px(62)
                onCreateServer: createServerDialog.open()
                onJoinServer: joinServerDialog.open()
            }

            ChannelSidebar {
                id: sidebar
                Layout.fillHeight: true
                Layout.preferredWidth: Theme.px(236)
                onCreateChannel: parentId => {
                    createChannelDialog.parentId = parentId
                    createChannelDialog.open()
                }
                onOpenServerSettings: serverSettings.open()
                onNewGroup: peoplePicker.openCreate()
                onAddToGroup: id => peoplePicker.openAdd(id, App.channelRecipients(id))
                onRenameGroup: (id, name) => {
                    renameGroupDialog.channelId = id
                    renameGroupDialog.open()
                    renameGroupDialog.value = name
                }
                onOpenChannelPermissions: (id, name) => channelPermissions.openFor(id, name)
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                StreamPanel {
                    id: streams
                    visible: App.watchedStreams.length > 0 || App.sharingScreen
                    Layout.fillWidth: true
                    Layout.fillHeight: expanded
                    Layout.preferredHeight: expanded ? -1 : root.height * 0.55
                    onToggleExpanded: expanded = !expanded
                }
                ChatPane {
                    id: chat
                    visible: !(streams.visible && streams.expanded)
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    membersVisible: root.showMembers
                    onToggleMembers: root.showMembers = !root.showMembers
                    onOpenSearch: searchPanel.open()
                }
            }

            MemberList {
                Layout.fillHeight: true
                Layout.preferredWidth: Theme.px(220)
                visible: root.showMembers
            }
        }

        BottomBar {
            Layout.fillWidth: true
            onOpenSettings: settingsDialog.open()
        }
    }

    // ------------------------------------------------------------ shortcuts
    Shortcut {
        sequence: App.shortcuts["quick_switcher"] || "Ctrl+K"
        onActivated: switcher.open()
    }
    Shortcut {
        sequence: App.shortcuts["toggle_mute"] || "Ctrl+Shift+M"
        onActivated: App.toggleMute()
    }
    Shortcut {
        sequence: App.shortcuts["toggle_deafen"] || "Ctrl+Shift+D"
        onActivated: App.toggleDeafen()
    }
    Shortcut {
        sequence: App.shortcuts["previous_channel"] || "Alt+Up"
        onActivated: App.selectRelativeChannel(-1)
    }
    Shortcut {
        sequence: App.shortcuts["next_channel"] || "Alt+Down"
        onActivated: App.selectRelativeChannel(1)
    }
    Shortcut {
        sequence: App.shortcuts["focus_channels"] || "Ctrl+L"
        onActivated: sidebar.focusList()
    }
    Shortcut {
        sequence: App.shortcuts["search"] || "Ctrl+F"
        onActivated: searchPanel.open()
    }
    Shortcut {
        sequence: App.shortcuts["command_help"] || "Ctrl+/"
        onActivated: commandHelp.open()
    }

    // --------------------------------------------------------------- dialogs
    QuickSwitcher { id: switcher }
    SettingsDialog { id: settingsDialog }
    CommandHelp { id: commandHelp }
    SearchPanel { id: searchPanel }
    ServerSettingsDialog { id: serverSettings }
    PeoplePickerDialog { id: peoplePicker }
    TextPromptDialog {
        id: renameGroupDialog
        property string channelId
        title: qsTr("Rename conversation")
        label: qsTr("Name")
        acceptText: qsTr("Rename")
        onAccepted: value => App.renameGroup(channelId, value)
    }
    ChannelPermissionsDialog { id: channelPermissions }
    CreateChannelDialog { id: createChannelDialog }

    TextPromptDialog {
        id: createServerDialog
        title: qsTr("Create a server")
        label: qsTr("Server name")
        placeholder: qsTr("Sleepy Studio")
        acceptText: qsTr("Create")
        onAccepted: value => App.createServer(value)
    }

    TextPromptDialog {
        id: joinServerDialog
        title: qsTr("Join a server")
        label: qsTr("Invite link or code")
        placeholder: "omachat://invite/…"
        acceptText: qsTr("Join")
        onAccepted: value => App.joinServer(value)
    }

    Component.onCompleted: chat.focusComposer()
}
