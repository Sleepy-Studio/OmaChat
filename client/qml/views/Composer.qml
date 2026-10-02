import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import OmaChat

// Message input.
//   Enter          send            Shift+Enter   newline
//   Up (empty)     edit last own   Tab           complete @user / #channel / /command
//   Esc            cancel reply or edit
//   Ctrl+V         paste text, or attach a copied image / files
Item {
    id: composer

    property string editingId
    signal editFinished()
    signal editLastRequested()

    implicitHeight: column.implicitHeight + Metrics.px(16)

    function focusInput() { input.forceActiveFocus() }

    function beginEdit(id, text) {
        input.text = text
        input.cursorPosition = input.length
        input.forceActiveFocus()
    }

    function finishEdit() {
        input.text = ""
        composer.editFinished()
    }

    function submit() {
        const text = input.text
        if (text.trim().length === 0 && (composer.editingId.length > 0 || App.pendingFiles.length === 0))
            return
        if (composer.editingId.length > 0) {
            App.editMessage(composer.editingId, text)
            finishEdit()
            return
        }
        if (App.sendComposer(text)) {
            input.text = ""
            if (App.messages.anchorMessageId.length > 0)
                App.messages.reload()
        }
    }

    // Tab completion state: cycles through candidates on repeated Tab.
    property var completion: null

    function complete() {
        const before = input.text.substring(0, input.cursorPosition)
        if (!completion || completion.cursor !== input.cursorPosition) {
            const result = App.complete(before)
            if (!result.candidates || result.candidates.length === 0)
                return false
            completion = { start: result.start, candidates: result.candidates, index: 0, cursor: -1 }
        } else {
            completion.index = (completion.index + 1) % completion.candidates.length
        }
        const replacement = completion.candidates[completion.index] + " "
        const end = input.cursorPosition
        const prefixStart = completion.start
        const currentEnd = completion.cursor >= 0 ? completion.cursor : end
        input.remove(prefixStart, currentEnd)
        input.insert(prefixStart, replacement)
        completion.cursor = prefixStart + replacement.length
        input.cursorPosition = completion.cursor
        return true
    }

    Connections {
        target: App
        function onComposerRestore(text) {
            if (input.text.length === 0)
                input.text = text
        }
    }

    FileDialog {
        id: filePicker
        title: qsTr("Attach files")
        fileMode: FileDialog.OpenFiles
        onAccepted: App.addFiles(selectedFiles)
    }

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Metrics.px(16)
        anchors.rightMargin: Metrics.px(16)
        anchors.bottomMargin: Metrics.px(12)
        spacing: 0

        // Reply / edit banner
        Rectangle {
            visible: App.replyToId.length > 0 || composer.editingId.length > 0
            Layout.fillWidth: true
            implicitHeight: Metrics.px(30)
            color: Theme.surface
            radius: Metrics.px(6)
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Metrics.px(10)
                anchors.rightMargin: Metrics.px(4)
                Icon { name: composer.editingId.length > 0 ? "edit" : "reply"; size: Metrics.px(14) }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    color: Theme.textMuted
                    font.pixelSize: Metrics.px(12)
                    text: composer.editingId.length > 0 ? qsTr("Editing message — Enter to save, Esc to cancel")
                                                        : qsTr("Replying to %1").arg(App.replyToPreview)
                }
                IconButton {
                    implicitWidth: Metrics.px(24)
                    implicitHeight: Metrics.px(24)
                    iconSize: Metrics.px(14)
                    iconName: "x"
                    tip: qsTr("Cancel")
                    onClicked: composer.editingId.length > 0 ? composer.finishEdit() : App.cancelReply()
                }
            }
        }

        // Files waiting to go out with the next message
        Flow {
            visible: App.pendingFiles.length > 0
            Layout.fillWidth: true
            Layout.bottomMargin: Metrics.px(6)
            spacing: Metrics.px(6)
            Repeater {
                model: App.pendingFiles
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    implicitHeight: Metrics.px(30)
                    implicitWidth: Math.min(chipRow.implicitWidth + Metrics.px(12), Metrics.px(280))
                    radius: Metrics.px(6)
                    color: Theme.surface
                    border.color: Theme.border
                    Row {
                        id: chipRow
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: Metrics.px(8)
                        spacing: Metrics.px(6)
                        Icon { name: "file"; size: Metrics.px(14); anchors.verticalCenter: parent.verticalCenter }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(implicitWidth, Metrics.px(170))
                            elide: Text.ElideMiddle
                            color: Theme.text
                            font.pixelSize: Metrics.px(12)
                            text: modelData.name
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            color: Theme.textMuted
                            font.pixelSize: Metrics.px(11)
                            text: App.formatSize(modelData.size)
                        }
                        IconButton {
                            anchors.verticalCenter: parent.verticalCenter
                            implicitWidth: Metrics.px(20)
                            implicitHeight: Metrics.px(20)
                            iconSize: Metrics.px(12)
                            iconName: "x"
                            tip: qsTr("Remove %1").arg(modelData.name)
                            onClicked: App.removePendingFile(index)
                        }
                    }
                }
            }
        }

        // Uploads in flight (they continue even if this window closes)
        Repeater {
            model: App.uploads
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                Layout.bottomMargin: Metrics.px(4)
                spacing: Metrics.px(8)
                Text {
                    Layout.preferredWidth: Metrics.px(180)
                    elide: Text.ElideMiddle
                    color: Theme.textMuted
                    font.pixelSize: Metrics.px(12)
                    text: modelData.waiting ? qsTr("%1 continues when reconnected").arg(modelData.name)
                                            : qsTr("Uploading %1").arg(modelData.name)
                }
                ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(1, modelData.total)
                    value: modelData.transferred
                }
                Text {
                    color: Theme.textMuted
                    font.pixelSize: Metrics.px(11)
                    text: App.formatSize(modelData.transferred) + " / " + App.formatSize(modelData.total)
                }
                IconButton {
                    implicitWidth: Metrics.px(22)
                    implicitHeight: Metrics.px(22)
                    iconSize: Metrics.px(12)
                    iconName: "x"
                    tip: qsTr("Cancel upload")
                    onClicked: App.cancelTransfer(modelData.id)
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Math.min(input.implicitHeight, Metrics.px(220)) + Metrics.px(4)
            radius: Metrics.px(8)
            color: Theme.surfaceAlt
            border.width: input.activeFocus ? 1 : 0
            border.color: Theme.border

            ScrollView {
                id: scroll
                anchors.fill: parent
                anchors.rightMargin: Metrics.px(144)
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                TextArea {
                    id: input
                    enabled: App.canSend || composer.editingId.length > 0
                    wrapMode: TextArea.Wrap
                    color: Theme.text
                    placeholderText: !App.canSend ? qsTr("You do not have permission to send messages here")
                                   : App.selectedChannelType === "group_dm" ? qsTr("Message %1").arg(App.selectedChannelName)
                                   : App.homeSelected ? qsTr("Message @%1").arg(App.selectedChannelName)
                                   : qsTr("Message #%1").arg(App.selectedChannelName)
                    placeholderTextColor: Theme.textMuted
                    selectionColor: Theme.accent
                    selectedTextColor: Theme.accentText
                    font.pixelSize: Metrics.px(14)
                    leftPadding: Metrics.px(12)
                    topPadding: Metrics.px(10)
                    bottomPadding: Metrics.px(10)
                    background: null
                    Accessible.name: placeholderText

                    onTextChanged: {
                        if (text.length > 0 && composer.editingId.length === 0 && !text.startsWith("/"))
                            App.notifyTyping()
                    }
                    onCursorPositionChanged: if (composer.completion && composer.completion.cursor !== cursorPosition) composer.completion = null

                    Keys.onPressed: event => {
                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                                && !(event.modifiers & Qt.ShiftModifier)) {
                            composer.submit()
                            event.accepted = true
                        } else if (event.key === Qt.Key_Up && input.length === 0 && composer.editingId.length === 0) {
                            composer.editLastRequested()
                            event.accepted = true
                        } else if (event.matches(StandardKey.Paste) && composer.editingId.length === 0) {
                            // Images and copied files become attachments; text pastes normally.
                            event.accepted = App.pasteAttachment()
                        } else if (event.key === Qt.Key_Tab) {
                            // Only consume Tab when completing; otherwise it moves focus.
                            event.accepted = composer.complete()
                        } else if (event.key === Qt.Key_Escape) {
                            if (composer.editingId.length > 0) {
                                composer.finishEdit()
                                event.accepted = true
                            } else if (App.replyToId.length > 0) {
                                App.cancelReply()
                                event.accepted = true
                            }
                        }
                    }
                }
            }

            IconButton {
                id: emojiButton
                anchors.right: attachButton.left
                anchors.bottom: parent.bottom
                anchors.margins: Metrics.px(4)
                iconName: "smile"
                tip: qsTr("Insert emoji")
                enabled: App.canSend
                onClicked: composerEmojiPicker.open()

                EmojiPicker {
                    id: composerEmojiPicker
                    x: parent.width - width
                    y: -height - Metrics.px(4)
                    onEmojiSelected: glyph => {
                        input.insert(input.cursorPosition, glyph)
                        input.forceActiveFocus()
                    }
                }
            }

            IconButton {
                id: attachButton
                anchors.right: sendButton.left
                anchors.bottom: parent.bottom
                anchors.margins: Metrics.px(4)
                iconName: "paperclip"
                tip: App.attachmentsSupported ? qsTr("Attach files") : qsTr("This server does not accept attachments")
                enabled: App.canSend && App.attachmentsSupported && composer.editingId.length === 0
                onClicked: filePicker.open()
            }

            FlatButton {
                id: sendButton
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: Metrics.px(4)
                primary: true
                text: composer.editingId.length > 0 ? qsTr("Save") : qsTr("Send")
                enabled: composer.editingId.length > 0 ? input.text.trim().length > 0
                       : App.canSend && (input.text.trim().length > 0 || App.pendingFiles.length > 0)
                onClicked: composer.submit()
                ToolTip.visible: hovered
                ToolTip.text: composer.editingId.length > 0 ? qsTr("Save edit with Enter")
                              : qsTr("Send with Enter; Shift+Enter adds a line")
            }

            DropArea {
                anchors.fill: parent
                enabled: App.canSend && composer.editingId.length === 0
                onEntered: drag => {
                    if (drag.hasUrls)
                        drag.accept(Qt.CopyAction)
                    else
                        drag.accepted = false
                }
                onDropped: drop => {
                    if (drop.hasUrls) {
                        App.addFiles(drop.urls)
                        drop.accept(Qt.CopyAction)
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    visible: parent.containsDrag
                    radius: Metrics.px(8)
                    color: "transparent"
                    border.color: Theme.accent
                    border.width: 2
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: input.activeFocus && input.enabled
            text: composer.editingId.length > 0 ? qsTr("Enter to save · Esc to cancel")
                  : qsTr("Enter to send · Shift+Enter for a new line")
            color: Theme.textMuted
            font.pixelSize: Metrics.px(11)
        }
    }
}
