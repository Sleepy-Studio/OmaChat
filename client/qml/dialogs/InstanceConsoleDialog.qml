import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

Dialog {
    id: dialog
    title: qsTr("Instance console")
    width: Math.min(Metrics.px(820), (parent ? parent.width : 900) - Metrics.px(32))
    height: Math.min(Metrics.px(660), (parent ? parent.height : 700) - Metrics.px(32))
    onAboutToShow: { tabs.currentIndex = 0; App.refreshInstanceStatus() }

    readonly property var status: App.instanceStatus
    readonly property var communities: status.communities || []
    readonly property var users: status.users || []
    readonly property var audit: status.audit || []
    readonly property var logs: status.logs || []
    property string selectedCommunityId: ""
    readonly property var selectedCommunity: {
        for (let i = 0; i < communities.length; ++i)
            if (communities[i].id === selectedCommunityId)
                return communities[i]
        return communities.length > 0 ? communities[0] : null
    }
    readonly property var selectedMembers: {
        if (!selectedCommunity) return []
        const ids = selectedCommunity.member_ids || []
        return users.filter(u => ids.indexOf(u.id) >= 0)
    }
    readonly property var selectedBans: {
        if (!selectedCommunity) return []
        const ids = selectedCommunity.banned_user_ids || []
        return users.filter(u => ids.indexOf(u.id) >= 0)
    }

    component Label: Text {
        color: Theme.textMuted
        font.pixelSize: Metrics.px(12)
    }

    component Metric: Rectangle {
        id: metric
        property string caption
        property string value
        Layout.fillWidth: true
        implicitHeight: Metrics.px(74)
        radius: Metrics.px(6)
        color: Theme.surfaceAlt
        border.color: Theme.border
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Metrics.px(12)
            spacing: Metrics.px(3)
            Text { text: metric.value; color: Theme.text; font.bold: true; font.pixelSize: Metrics.px(21) }
            Label { text: metric.caption }
        }
    }

    contentItem: ColumnLayout {
        spacing: Metrics.px(12)

        RowLayout {
            Layout.fillWidth: true
            Icon { name: "shield"; size: Metrics.px(20); color: Theme.accent }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text { text: dialog.title; color: Theme.text; font.bold: true; font.pixelSize: Metrics.px(17) }
                Label { text: dialog.status.instance_name || App.instanceName }
            }
            IconButton { iconName: "x"; tip: qsTr("Close"); onClicked: dialog.close() }
        }

        TabBar {
            id: tabs
            Layout.fillWidth: true
            background: Rectangle { color: "transparent" }
            Repeater {
                model: [qsTr("Overview"), qsTr("Accounts"), qsTr("Communities"), qsTr("Audit"), qsTr("Logs"), qsTr("Service")]
                delegate: TabButton {
                    id: tab
                    required property string modelData
                    text: modelData
                    font.pixelSize: Metrics.px(12)
                    contentItem: Text {
                        text: tab.text
                        font: tab.font
                        horizontalAlignment: Text.AlignHCenter
                        color: tab.checked ? Theme.text : Theme.textMuted
                    }
                    background: Rectangle {
                        color: "transparent"
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 2
                            color: tab.checked ? Theme.accent : Theme.border
                        }
                    }
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            ScrollView {
                clip: true
                ColumnLayout {
                    width: parent.width
                    spacing: Metrics.px(12)
                    Label { text: qsTr("Live instance health") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Metrics.px(8)
                        Metric { caption: qsTr("Online users"); value: String(dialog.status.online_users || 0) }
                        Metric { caption: qsTr("Sessions"); value: String(dialog.status.connected_sessions || 0) }
                        Metric { caption: qsTr("Accounts"); value: String(dialog.status.total_users || 0) }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Metrics.px(8)
                        Metric { caption: qsTr("Communities"); value: String(dialog.communities.length) }
                        Metric { caption: qsTr("Messages stored"); value: Number(dialog.status.total_messages || 0).toLocaleString() }
                    }
                    Label {
                        text: qsTr("Running since %1 · version %2")
                                  .arg(dialog.status.started_at ? new Date(dialog.status.started_at).toLocaleString() : "—")
                                  .arg(dialog.status.version || "—")
                    }
                    FlatButton { text: qsTr("Refresh status"); onClicked: App.refreshInstanceStatus() }
                }
            }

            ListView {
                id: accountList
                clip: true
                spacing: Metrics.px(5)
                model: dialog.users
                delegate: Rectangle {
                    id: accountRow
                    required property var modelData
                    width: accountList.width
                    height: Metrics.px(56)
                    radius: Metrics.px(5)
                    color: Theme.surfaceAlt
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Metrics.px(10)
                        spacing: Metrics.px(10)
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text {
                                text: accountRow.modelData.display_name
                                color: Theme.text
                                font.bold: true
                                font.pixelSize: Metrics.px(13)
                                elide: Text.ElideRight
                            }
                            Label { text: "@" + accountRow.modelData.username }
                        }
                        Label {
                            text: accountRow.modelData.suspended ? qsTr("Suspended")
                                  : accountRow.modelData.online ? qsTr("Online") : qsTr("Offline")
                            color: accountRow.modelData.suspended ? Theme.danger
                                   : accountRow.modelData.online ? Theme.success : Theme.textMuted
                        }
                        FlatButton {
                            visible: accountRow.modelData.id !== App.selfId
                            text: accountRow.modelData.suspended ? qsTr("Restore") : qsTr("Suspend")
                            danger: !accountRow.modelData.suspended
                            onClicked: {
                                confirmAction.kind = "suspension"
                                confirmAction.targetId = accountRow.modelData.id
                                confirmAction.targetName = accountRow.modelData.username
                                confirmAction.suspend = !accountRow.modelData.suspended
                                confirmAction.open()
                            }
                        }
                    }
                }
                Label { visible: accountList.count === 0; anchors.centerIn: parent; text: qsTr("No accounts to show") }
            }

            RowLayout {
                spacing: Metrics.px(12)
                ListView {
                    id: communityList
                    Layout.preferredWidth: Metrics.px(245)
                    Layout.fillHeight: true
                    clip: true
                    spacing: Metrics.px(4)
                    model: dialog.communities
                    delegate: Rectangle {
                        id: communityRow
                        required property var modelData
                        width: communityList.width
                        height: Metrics.px(55)
                        radius: Metrics.px(5)
                        color: dialog.selectedCommunity && dialog.selectedCommunity.id === modelData.id
                               ? Theme.selection : Theme.surfaceAlt
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: Metrics.px(8)
                            spacing: 2
                            Text { text: communityRow.modelData.name; color: Theme.text; font.bold: true; font.pixelSize: Metrics.px(13); elide: Text.ElideRight }
                            Label { text: qsTr("%1 members · %2 channels").arg(communityRow.modelData.members).arg(communityRow.modelData.channels) }
                        }
                        MouseArea { anchors.fill: parent; onClicked: dialog.selectedCommunityId = communityRow.modelData.id }
                    }
                }
                Rectangle { Layout.fillHeight: true; Layout.preferredWidth: 1; color: Theme.border }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: dialog.selectedCommunity !== null
                    spacing: Metrics.px(7)
                    Text { text: dialog.selectedCommunity ? dialog.selectedCommunity.name : ""; color: Theme.text; font.bold: true; font.pixelSize: Metrics.px(15) }
                    Label { text: dialog.selectedCommunity ? qsTr("Owned by @%1").arg(dialog.selectedCommunity.owner_name) : "" }
                    SectionLabel { text: qsTr("MEMBERS") }
                    ListView {
                        id: selectedMemberList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: dialog.selectedMembers
                        delegate: RowLayout {
                            id: memberRow
                            required property var modelData
                            width: selectedMemberList.width
                            height: Metrics.px(34)
                            Text { text: "@" + memberRow.modelData.username; color: Theme.text; Layout.fillWidth: true; font.pixelSize: Metrics.px(12) }
                            FlatButton {
                                visible: dialog.selectedCommunity && memberRow.modelData.id !== dialog.selectedCommunity.owner_id
                                         && memberRow.modelData.id !== App.selfId
                                text: qsTr("Kick")
                                onClicked: dialog.confirmModeration("kick", memberRow.modelData)
                            }
                            FlatButton {
                                visible: dialog.selectedCommunity && memberRow.modelData.id !== dialog.selectedCommunity.owner_id
                                         && memberRow.modelData.id !== App.selfId
                                text: qsTr("Ban")
                                danger: true
                                onClicked: dialog.confirmModeration("ban", memberRow.modelData)
                            }
                        }
                    }
                    SectionLabel { text: qsTr("BANNED ACCOUNTS"); visible: dialog.selectedBans.length > 0 }
                    ListView {
                        id: selectedBanList
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(contentHeight, Metrics.px(120))
                        visible: dialog.selectedBans.length > 0
                        clip: true
                        model: dialog.selectedBans
                        delegate: RowLayout {
                            id: bannedRow
                            required property var modelData
                            width: selectedBanList.width
                            height: Metrics.px(30)
                            Label { text: "@" + bannedRow.modelData.username; Layout.fillWidth: true }
                            FlatButton { text: qsTr("Unban"); onClicked: dialog.confirmModeration("unban", bannedRow.modelData) }
                        }
                    }
                    FlatButton {
                        text: qsTr("Delete community")
                        danger: true
                        onClicked: {
                            confirmAction.kind = "community"
                            confirmAction.targetId = dialog.selectedCommunity.id
                            confirmAction.targetName = dialog.selectedCommunity.name
                            confirmAction.open()
                        }
                    }
                }
                Label { visible: dialog.communities.length === 0; Layout.fillWidth: true; text: qsTr("No communities on this instance") }
            }

            ListView {
                id: auditList
                clip: true
                spacing: Metrics.px(3)
                model: dialog.audit
                delegate: Rectangle {
                    id: auditRow
                    required property var modelData
                    width: auditList.width
                    height: Metrics.px(38)
                    color: Theme.surfaceAlt
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Metrics.px(8)
                        Label { text: new Date(auditRow.modelData.at).toLocaleString(); Layout.preferredWidth: Metrics.px(168) }
                        Text { text: auditRow.modelData.action; color: Theme.text; Layout.fillWidth: true; font.pixelSize: Metrics.px(12) }
                        Label { text: auditRow.modelData.target_id === "0" ? "" : auditRow.modelData.target_id }
                    }
                }
                Label { visible: auditList.count === 0; anchors.centerIn: parent; text: qsTr("Operator actions will appear here") }
            }

            ListView {
                id: logList
                clip: true
                spacing: Metrics.px(2)
                model: dialog.logs
                delegate: Text {
                    required property string modelData
                    width: logList.width
                    text: modelData
                    color: Theme.textMuted
                    font.family: "monospace"
                    font.pixelSize: Metrics.px(11)
                    wrapMode: Text.WrapAnywhere
                }
                Label { visible: logList.count === 0; anchors.centerIn: parent; text: qsTr("No recent process logs") }
            }

            ScrollView {
                clip: true
                ColumnLayout {
                    width: parent.width
                    spacing: Metrics.px(16)
                    SectionLabel { text: qsTr("ACCESS") }
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: qsTr("Public registration"); color: Theme.text; font.pixelSize: Metrics.px(13) }
                            Label { text: qsTr("Controls creation of new accounts, including OAuth sign-up.") }
                        }
                        Switch {
                            checked: !!dialog.status.registration_open
                            onClicked: App.setInstanceRegistration(checked)
                        }
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
                    SectionLabel { text: qsTr("SERVICE") }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("Restart asks the host supervisor to bring this process back. Existing connections will reconnect. Available only when enabled in server configuration.")
                    }
                    FlatButton {
                        text: qsTr("Restart instance")
                        enabled: !!dialog.status.restart_available
                        onClicked: { confirmAction.kind = "restart"; confirmAction.open() }
                    }
                }
            }
        }
    }

    ConfirmDialog {
        id: confirmAction
        property string kind
        property string targetId
        property string targetName
        property bool suspend
        property string communityId
        title: kind === "restart" ? qsTr("Restart instance?")
               : kind === "community" ? qsTr("Delete %1?").arg(targetName)
               : kind === "moderation" ? qsTr("%1 @%2?").arg(confirmText).arg(targetName)
               : suspend ? qsTr("Suspend @%1?").arg(targetName) : qsTr("Restore @%1?").arg(targetName)
        message: kind === "restart" ? qsTr("Everyone will briefly disconnect while the service restarts.")
                 : kind === "community" ? qsTr("This permanently deletes its channels, messages, and membership.")
                 : kind === "moderation" ? qsTr("This action affects the selected community and is recorded in the audit trail.")
                 : suspend ? qsTr("This signs the account out on every device and blocks new logins.")
                 : qsTr("This account will be able to log in again.")
        confirmText: kind === "restart" ? qsTr("Restart")
                     : kind === "community" ? qsTr("Delete")
                     : kind === "moderation" ? moderationAction.charAt(0).toUpperCase() + moderationAction.slice(1)
                     : suspend ? qsTr("Suspend") : qsTr("Restore")
        property string moderationAction
        destructive: kind === "community" || kind === "restart" || suspend
                     || (kind === "moderation" && moderationAction !== "unban")
        onConfirmed: {
            if (kind === "restart") App.restartInstance()
            else if (kind === "community") App.deleteInstanceCommunity(targetId)
            else if (kind === "moderation") App.moderateInstanceCommunity(communityId, targetId, moderationAction)
            else App.setInstanceSuspension(targetId, suspend)
        }
    }

    function confirmModeration(action, user) {
        confirmAction.kind = "moderation"
        confirmAction.communityId = selectedCommunity.id
        confirmAction.targetId = user.id
        confirmAction.targetName = user.username
        confirmAction.moderationAction = action
        confirmAction.open()
    }
}
