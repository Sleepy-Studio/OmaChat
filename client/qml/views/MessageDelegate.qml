import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// One message. Grouped messages (same author within five minutes) omit the
// avatar and name. Content is pre-sanitized rich text from the controller.
Item {
    id: root

    required property int index
    required property string messageId
    required property string authorId
    required property string authorName
    required property string authorColor
    required property string content
    required property string html
    required property string timeText
    required property string dayText
    required property bool edited
    required property bool isAction
    required property string replyTo
    required property string replyPreview
    required property bool mentionsMe
    required property var reactions
    required property var attachments
    required property bool groupStart
    required property bool dayStart
    required property bool isOwn
    required property string e2e

    property bool editing: false
    signal editRequested(string id)

    readonly property bool hovered: hover.hovered || actions.hovered
    readonly property bool canDelete: isOwn || App.canManageMessages

    implicitHeight: column.implicitHeight

    Accessible.role: Accessible.ListItem
    Accessible.name: authorName + ", " + timeText + ": " + content

    HoverHandler { id: hover }

    ColumnLayout {
        id: column
        width: parent.width
        spacing: 0

        // Day separator
        RowLayout {
            visible: root.dayStart
            Layout.fillWidth: true
            Layout.topMargin: Theme.px(12)
            Layout.bottomMargin: Theme.px(4)
            Layout.leftMargin: Theme.px(16)
            Layout.rightMargin: Theme.px(16)
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
            Text {
                text: root.dayText
                color: Theme.textFaint
                font.pixelSize: Theme.px(11)
                font.bold: true
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: root.groupStart ? Theme.px(10) : 0
            implicitHeight: body.implicitHeight + Theme.px(root.groupStart ? 6 : 3)
            color: root.editing ? Theme.selection
                 : root.mentionsMe ? Qt.rgba(Theme.mention.r, Theme.mention.g, Theme.mention.b, 0.10)
                 : (root.hovered ? Theme.surface : "transparent")

            Rectangle {
                visible: root.mentionsMe
                width: Theme.px(2)
                height: parent.height
                color: Theme.mention
            }

            ColumnLayout {
                id: body
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: Theme.px(2)
                anchors.leftMargin: Theme.px(16)
                anchors.rightMargin: Theme.px(16)
                spacing: Theme.px(2)

                // Reply reference
                RowLayout {
                    visible: root.replyTo.length > 0
                    Layout.leftMargin: Theme.px(46)
                    spacing: Theme.px(6)
                    Icon { name: "reply"; size: Theme.px(12); color: Theme.textFaint }
                    Text {
                        Layout.fillWidth: true
                        text: root.replyPreview
                        color: Theme.textMuted
                        font.pixelSize: Theme.px(12)
                        elide: Text.ElideRight
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.px(10)

                    // Avatar column (fixed width keeps grouped text aligned)
                    Item {
                        Layout.alignment: Qt.AlignTop
                        implicitWidth: Theme.px(36)
                        implicitHeight: root.groupStart ? Theme.px(36) : Theme.px(18)
                        Avatar {
                            visible: root.groupStart
                            userId: root.authorId
                            name: root.authorName
                            size: Theme.px(36)
                        }
                        Text {
                            visible: !root.groupStart && root.hovered
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.timeText
                            color: Theme.textFaint
                            font.pixelSize: Theme.px(10)
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.px(2)

                        RowLayout {
                            visible: root.groupStart
                            spacing: Theme.px(8)
                            Text {
                                text: root.authorName
                                color: root.authorColor
                                font.pixelSize: Theme.px(14)
                                font.bold: true
                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.RightButton | Qt.LeftButton
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: if (!root.isOwn) App.openDm(root.authorId)
                                }
                            }
                            Text {
                                text: root.timeText
                                color: Theme.textFaint
                                font.pixelSize: Theme.px(11)
                            }
                            Icon {
                                // Decrypted, but the sending device is not one we know for this person.
                                visible: root.e2e === "unverified"
                                name: "alert"
                                size: Theme.px(13)
                                color: Theme.warning
                                HoverHandler { id: unverifiedHover }
                                ToolTip.visible: unverifiedHover.hovered
                                ToolTip.text: qsTr("Sent from a device OmaChat does not know for %1. Compare safety numbers.").arg(root.authorName)
                            }
                        }
                        Text {
                            visible: root.e2e === "undecryptable"
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: qsTr("🔒 Encrypted for other devices. This device joined after the message was sent, or its key was lost.")
                            color: Theme.textFaint
                            font.italic: true
                            font.pixelSize: Theme.px(13)
                        }

                        TextEdit {
                            id: text
                            visible: root.content.length > 0 || root.isAction
                            Layout.fillWidth: true
                            readOnly: true
                            selectByMouse: true
                            persistentSelection: false
                            textFormat: TextEdit.RichText
                            wrapMode: TextEdit.Wrap
                            text: root.isAction ? "<i>" + root.authorName + " " + root.html + "</i>" : root.html
                            color: Theme.text
                            selectionColor: Theme.accent
                            selectedTextColor: Theme.accentText
                            font.pixelSize: Theme.px(14)
                            onLinkActivated: link => App.openLink(link)
                            Accessible.ignored: true

                            HoverHandler {
                                cursorShape: text.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.IBeamCursor
                            }
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: root.ListView.view.openMenu(root)
                            }
                        }

                        // Attachments: images preview inline, other files are cards.
                        Flow {
                            visible: root.attachments && root.attachments.length > 0
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.px(2)
                            spacing: Theme.px(6)
                            Repeater {
                                model: root.attachments
                                delegate: AttachmentView {
                                    required property var modelData
                                    attachment: modelData
                                    maxWidth: body.width - Theme.px(46)
                                }
                            }
                        }

                        Text {
                            visible: root.edited
                            text: qsTr("(edited)")
                            color: Theme.textFaint
                            font.pixelSize: Theme.px(10)
                        }

                        // Reactions
                        Flow {
                            visible: root.reactions && root.reactions.length > 0
                            Layout.fillWidth: true
                            spacing: Theme.px(4)
                            Repeater {
                                model: root.reactions
                                delegate: Rectangle {
                                    required property var modelData
                                    implicitHeight: Theme.px(24)
                                    implicitWidth: rlabel.implicitWidth + Theme.px(14)
                                    radius: Theme.px(6)
                                    color: modelData.me ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18) : Theme.surface
                                    border.color: modelData.me ? Theme.accent : Theme.border
                                    Text {
                                        id: rlabel
                                        anchors.centerIn: parent
                                        text: parent.modelData.emoji + " " + parent.modelData.count
                                        color: Theme.text
                                        font.pixelSize: Theme.px(12)
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: App.toggleReaction(root.messageId, parent.modelData.emoji)
                                    }
                                    Accessible.role: Accessible.Button
                                    Accessible.name: qsTr("%1 reaction, %2").arg(modelData.emoji).arg(modelData.count)
                                }
                            }
                        }
                    }
                }
            }

            // Hover actions
            Rectangle {
                id: actions
                readonly property bool hovered: actionsHover.hovered
                visible: root.hovered && !root.editing
                anchors.right: parent.right
                anchors.rightMargin: Theme.px(16)
                anchors.top: parent.top
                anchors.topMargin: -Theme.px(14)
                implicitWidth: actionRow.implicitWidth + Theme.px(4)
                implicitHeight: Theme.px(30)
                radius: Theme.px(6)
                color: Theme.raised
                border.color: Theme.border
                z: 5
                HoverHandler { id: actionsHover }
                Row {
                    id: actionRow
                    anchors.centerIn: parent
                    IconButton { implicitWidth: Theme.px(28); implicitHeight: Theme.px(26); iconSize: Theme.px(15); iconName: "smile"; tip: qsTr("React 👍"); onClicked: App.toggleReaction(root.messageId, "👍") }
                    IconButton { implicitWidth: Theme.px(28); implicitHeight: Theme.px(26); iconSize: Theme.px(15); iconName: "reply"; tip: qsTr("Reply"); visible: App.canSend; onClicked: App.startReply(root.messageId) }
                    IconButton { implicitWidth: Theme.px(28); implicitHeight: Theme.px(26); iconSize: Theme.px(15); iconName: "edit"; tip: qsTr("Edit"); visible: root.isOwn; onClicked: root.editRequested(root.messageId) }
                    IconButton { implicitWidth: Theme.px(28); implicitHeight: Theme.px(26); iconSize: Theme.px(15); iconName: "copy"; tip: qsTr("Copy text"); onClicked: App.copyText(root.content) }
                    IconButton { implicitWidth: Theme.px(28); implicitHeight: Theme.px(26); iconSize: Theme.px(15); iconName: "trash"; tip: qsTr("Delete"); danger: true; visible: root.canDelete; onClicked: root.ListView.view.confirmDelete(root.messageId) }
                }
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                z: -1
                onClicked: root.ListView.view.openMenu(root)
            }
        }
    }
}
