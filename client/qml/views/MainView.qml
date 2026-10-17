pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// Dense desktop layout; secondary panes become drawers when space is tight.
Item {
    id: root

    property bool membersRequested: true
    property bool serverRailCollapsed: false
    readonly property bool dockMembers: width >= Metrics.px(1080) && membersRequested

    function openChannels() {
        const currentSidebar = sidebarDock.item as ChannelSidebar
        if (currentSidebar)
            currentSidebar.focusList()
    }

    function toggleMembers() {
        if (width < Metrics.px(1080)) {
            if (membersDrawer.opened)
                membersDrawer.close()
            else
                membersDrawer.open()
        } else {
            membersRequested = !membersRequested
        }
    }

    onDockMembersChanged: if (dockMembers) membersDrawer.close()

    Component {
        id: sidebarContent
        ChannelSidebar {
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
            onOpenChannelSettings: id => channelSettings.openFor(id)
            onOpenChannelDetails: id => channelDetails.openFor(id)
            onDuplicateChannel: id => duplicateChannelDialog.openFor(id)
        }
    }

    Connections {
        target: App
        function onSelectionChanged() { membersDrawer.close() }
    }

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

        StatusBanner { id: statusBanner; Layout.fillWidth: true }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            ServerRail {
                id: rail
                collapsed: root.serverRailCollapsed
                Layout.fillHeight: true
                Layout.preferredWidth: Metrics.px(root.serverRailCollapsed ? 30 : 62)
                onToggleCollapsed: root.serverRailCollapsed = !root.serverRailCollapsed
                onCreateServer: createServerDialog.open()
                onJoinServer: joinServerDialog.open()
            }

            Loader {
                id: sidebarDock
                active: true
                Layout.fillHeight: true
                Layout.preferredWidth: Metrics.px(236)
                visible: true
                sourceComponent: sidebarContent
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
                    membersVisible: root.dockMembers || membersDrawer.opened
                    showChannelsButton: false
                    onToggleChannels: root.openChannels()
                    onBrowseChannels: {
                        if (App.homeSelected)
                            switcher.open()
                        else
                            root.openChannels()
                    }
                    onToggleMembers: root.toggleMembers()
                    onOpenSearch: searchPanel.open()
                    onOpenChannelDetails: id => channelDetails.openFor(id)
                }
            }

            MemberList {
                Layout.fillHeight: true
                Layout.preferredWidth: Metrics.px(220)
                visible: root.dockMembers
            }
        }

        BottomBar {
            id: bottomBar
            Layout.fillWidth: true
            onOpenSettings: settingsDialog.open()
            onOpenVoiceboard: voiceboardDialog.open()
            onOpenChannelDetails: id => channelDetails.openFor(id)
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
        onActivated: root.openChannels()
    }
    Shortcut {
        sequence: App.shortcuts["search"] || "Ctrl+F"
        onActivated: searchPanel.open()
    }
    Shortcut {
        sequence: "Ctrl+Shift+V"
        enabled: App.voiceJoined
        onActivated: voiceboardDialog.open()
    }
    Shortcut {
        sequence: App.shortcuts["command_help"] || "Ctrl+/"
        onActivated: commandHelp.open()
    }
    // Stream zoom/pop-out: only live while a stream is actually shown, so
    // bare +/-/0 never fights with typing in the composer elsewhere.
    Shortcut {
        sequence: App.shortcuts["stream_zoom_in"] || "+"
        enabled: streams.visible
        onActivated: streams.zoomStep(1.2)
    }
    Shortcut {
        sequence: App.shortcuts["stream_zoom_in_alt"] || "="
        enabled: streams.visible
        onActivated: streams.zoomStep(1.2)
    }
    Shortcut {
        sequence: App.shortcuts["stream_zoom_out"] || "-"
        enabled: streams.visible
        onActivated: streams.zoomStep(1 / 1.2)
    }
    Shortcut {
        sequence: App.shortcuts["stream_zoom_reset"] || "0"
        enabled: streams.visible
        onActivated: streams.resetVideoZoom()
    }
    Shortcut {
        sequence: App.shortcuts["stream_pop_out"] || "Ctrl+Shift+P"
        enabled: streams.visible
        onActivated: streams.popOutCurrent()
    }

    // --------------------------------------------------------------- dialogs
    Popup {
        id: membersDrawer
        objectName: "membersDrawer"
        onOpened: Qt.callLater(() => { if (membersDrawer.opened) memberDrawerContent.focusList() })
        onClosed: chat.focusComposer()
        parent: Overlay.overlay
        x: root.width - width
        y: statusBanner.height
        width: Math.min(Metrics.px(250), root.width - Metrics.px(80))
        height: root.height - y - bottomBar.height
        padding: 0
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: Theme.surface; border.color: Theme.border }
        contentItem: MemberList { id: memberDrawerContent }
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animationMs } }
        exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.animationMs } }
    }
    QuickSwitcher { id: switcher }
    SettingsDialog { id: settingsDialog; objectName: "settingsDialog" }
    CommandHelp { id: commandHelp }
    SearchPanel { id: searchPanel }
    ServerSettingsDialog { id: serverSettings; objectName: "serverSettings" }
    InstanceConsoleDialog { id: instanceConsole }
    VoiceboardDialog { id: voiceboardDialog }
    Connections {
        target: rail
        function onOpenInstanceConsole() { instanceConsole.open() }
    }
    PeoplePickerDialog { id: peoplePicker }
    TextPromptDialog {
        id: renameGroupDialog
        property string channelId
        title: qsTr("Rename conversation")
        label: qsTr("Name")
        acceptText: qsTr("Rename")
        onAccepted: value => App.renameGroup(channelId, value)
    }
    ChannelPermissionsDialog {
        id: channelPermissions
        objectName: "channelPermissionsDialog"
        onClosed: { if (channelSettings.visible) channelSettings.contentItem.forceActiveFocus() }
    }
    ChannelSettingsDialog {
        id: channelSettings
        objectName: "channelSettingsDialog"
        onOpenPermissionsRequested: (id, name) => channelPermissions.openFor(id, name)
    }
    ChannelDetailsDialog { id: channelDetails; objectName: "channelDetailsDialog"; onEditRequested: id => channelSettings.openFor(id) }
    CreateChannelDialog { id: createChannelDialog }
    DuplicateChannelDialog { id: duplicateChannelDialog }

    CreateServerDialog {
        id: createServerDialog
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
