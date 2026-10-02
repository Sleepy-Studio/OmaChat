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
    property bool searchMatch: false
    readonly property bool keyboardCurrent: root.ListView.isCurrentItem && root.ListView.view.activeFocus
    signal editRequested(string id)

    readonly property bool hovered: hover.hovered || actions.hovered
    readonly property bool canDelete: isOwn || App.canManageMessages

    implicitHeight: column.implicitHeight

    Accessible.role: Accessible.ListItem
    Accessible.name: (searchMatch ? qsTr("Search match. ") : "") + authorName + ", " + timeText + ": " + content
                     + (keyboardCurrent ? qsTr(". Press Enter for message actions") : "")

    HoverHandler { id: hover }

    ColumnLayout {
        id: column
        width: parent.width
        spacing: 0

        // Day separator
        RowLayout {
            visible: root.dayStart
            Layout.fillWidth: true
            Layout.topMargin: Metrics.px(12)
            Layout.bottomMargin: Metrics.px(4)
            Layout.leftMargin: Metrics.px(16)
            Layout.rightMargin: Metrics.px(16)
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
            Text {
                text: root.dayText
                color: Theme.textMuted
                font.pixelSize: Metrics.px(11)
                font.bold: true
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: root.groupStart ? Metrics.px(10) : 0
            implicitHeight: body.implicitHeight + Metrics.px(root.groupStart ? 6 : 3)
            color: root.searchMatch ? Theme.selection
                 : root.editing ? Theme.selection
                 : root.mentionsMe ? Qt.rgba(Theme.mention.r, Theme.mention.g, Theme.mention.b, 0.10)
                 : (root.hovered ? Theme.surface : "transparent")
            border.width: root.keyboardCurrent ? Metrics.px(2) : 0
            border.color: Theme.accent

            Rectangle {
                visible: root.searchMatch
                width: Metrics.px(3)
                height: parent.height
                color: Theme.accent
            }

            Rectangle {
                visible: root.mentionsMe
                width: Metrics.px(2)
                height: parent.height
                color: Theme.mention
            }

            ColumnLayout {
                id: body
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: Metrics.px(2)
                anchors.leftMargin: Metrics.px(16)
                anchors.rightMargin: Metrics.px(16)
                spacing: Metrics.px(2)

                // Reply reference
                RowLayout {
                    visible: root.replyTo.length > 0
                    Layout.leftMargin: Metrics.px(46)
                    spacing: Metrics.px(6)
                    Icon { name: "reply"; size: Metrics.px(12); color: Theme.textMuted }
                    Text {
                        Layout.fillWidth: true
                        text: root.replyPreview
                        color: Theme.textMuted
                        font.pixelSize: Metrics.px(12)
                        elide: Text.ElideRight
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Metrics.px(10)

                    // Avatar column (fixed width keeps grouped text aligned)
                    Item {
                        Layout.alignment: Qt.AlignTop
                        implicitWidth: Metrics.px(36)
                        implicitHeight: root.groupStart ? Metrics.px(36) : Metrics.px(18)
                        Avatar {
                            visible: root.groupStart
                            userId: root.authorId
                            name: root.authorName
                            size: Metrics.px(36)
                        }
                        Text {
                            visible: !root.groupStart && root.hovered
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.timeText
                            color: Theme.textMuted
                            font.pixelSize: Metrics.px(10)
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Metrics.px(2)

                        RowLayout {
                            visible: root.groupStart
                            spacing: Metrics.px(8)
                            Text {
                                text: root.authorName
                                color: root.authorColor
                                font.pixelSize: Metrics.px(14)
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
                                color: Theme.textMuted
                                font.pixelSize: Metrics.px(11)
                            }
                            Icon {
                                // Decrypted, but the sending device is not one we know for this person.
                                visible: root.e2e === "unverified"
                                name: "alert"
                                size: Metrics.px(13)
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
                            color: Theme.textMuted
                            font.italic: true
                            font.pixelSize: Metrics.px(13)
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
                            font.pixelSize: Metrics.px(14)
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
                            Layout.topMargin: Metrics.px(2)
                            spacing: Metrics.px(6)
                            Repeater {
                                model: root.attachments
                                delegate: AttachmentView {
                                    required property var modelData
                                    attachment: modelData
                                    maxWidth: body.width - Metrics.px(46)
                                }
                            }
                        }

                        Text {
                            visible: root.edited
                            text: qsTr("(edited)")
                            color: Theme.textMuted
                            font.pixelSize: Metrics.px(10)
                        }

                        // Reactions
                        Flow {
                            visible: root.reactions && root.reactions.length > 0
                            Layout.fillWidth: true
                            spacing: Metrics.px(4)
                            Repeater {
                                model: root.reactions
                                delegate: Rectangle {
                                    id: pill
                                    required property var modelData
                                    readonly property bool isCustom: modelData.emoji.length > 2
                                        && modelData.emoji.startsWith(":") && modelData.emoji.endsWith(":")
                                    readonly property var custom: isCustom
                                        ? App.customEmojiByName(modelData.emoji.slice(1, -1)) : ({})
                                    readonly property bool hasCustomImage: isCustom && custom && custom.attachment_id
                                    implicitHeight: Metrics.px(24)
                                    implicitWidth: rrow.implicitWidth + Metrics.px(14)
                                    radius: Metrics.px(6)
                                    color: modelData.me ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18) : Theme.surface
                                    border.color: modelData.me ? Theme.accent : Theme.border
                                    Component.onCompleted: if (hasCustomImage) App.requestMedia(custom.attachment_id, custom.name)
                                    Row {
                                        id: rrow
                                        anchors.centerIn: parent
                                        spacing: Metrics.px(4)
                                        Image {
                                            visible: pill.hasCustomImage
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: Metrics.px(16)
                                            height: Metrics.px(16)
                                            fillMode: Image.PreserveAspectFit
                                            source: pill.hasCustomImage ? (App.previews[pill.custom.attachment_id] || "") : ""
                                        }
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: (pill.hasCustomImage ? "" : pill.modelData.emoji + " ") + pill.modelData.count
                                            color: Theme.text
                                            font.pixelSize: Metrics.px(12)
                                        }
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
                anchors.rightMargin: Metrics.px(16)
                anchors.top: parent.top
                anchors.topMargin: -Metrics.px(14)
                implicitWidth: actionRow.implicitWidth + Metrics.px(4)
                implicitHeight: Metrics.px(30)
                radius: Metrics.px(6)
                color: Theme.raised
                border.color: Theme.border
                z: 5
                HoverHandler { id: actionsHover }
                Row {
                    id: actionRow
                    anchors.centerIn: parent
                    IconButton {
                        implicitWidth: Metrics.px(28); implicitHeight: Metrics.px(26); iconSize: Metrics.px(15)
                        iconName: "smile"; tip: qsTr("Add reaction")
                        onClicked: reactionPicker.open()
                    }
                    IconButton { implicitWidth: Metrics.px(28); implicitHeight: Metrics.px(26); iconSize: Metrics.px(15); iconName: "reply"; tip: qsTr("Reply"); visible: App.canSend; onClicked: App.startReply(root.messageId) }
                    IconButton { implicitWidth: Metrics.px(28); implicitHeight: Metrics.px(26); iconSize: Metrics.px(15); iconName: "edit"; tip: qsTr("Edit"); visible: root.isOwn; onClicked: root.editRequested(root.messageId) }
                    IconButton { implicitWidth: Metrics.px(28); implicitHeight: Metrics.px(26); iconSize: Metrics.px(15); iconName: "copy"; tip: qsTr("Copy text"); onClicked: App.copyText(root.content) }
                    IconButton { implicitWidth: Metrics.px(28); implicitHeight: Metrics.px(26); iconSize: Metrics.px(15); iconName: "trash"; tip: qsTr("Delete"); danger: true; visible: root.canDelete; onClicked: root.ListView.view.confirmDelete(root.messageId) }
                }
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                z: -1
                onClicked: root.ListView.view.openMenu(root)
            }

            EmojiPicker {
                id: reactionPicker
                x: parent.width - width
                y: -height - Metrics.px(4)
                onEmojiSelected: glyph => App.toggleReaction(root.messageId, glyph)
            }
        }
    }
}
