import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// Message input.
//   Enter          send            Shift+Enter   newline
//   Up (empty)     edit last own   Tab           complete @user / #channel / /command
//   Esc            cancel reply or edit
Item {
    id: composer

    property string editingId
    signal editFinished()
    signal editLastRequested()

    implicitHeight: column.implicitHeight + Theme.px(16)

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
        if (text.trim().length === 0)
            return
        if (composer.editingId.length > 0) {
            App.editMessage(composer.editingId, text)
            finishEdit()
            return
        }
        if (App.sendComposer(text))
            input.text = ""
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

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.px(16)
        anchors.rightMargin: Theme.px(16)
        anchors.bottomMargin: Theme.px(12)
        spacing: 0

        // Reply / edit banner
        Rectangle {
            visible: App.replyToId.length > 0 || composer.editingId.length > 0
            Layout.fillWidth: true
            implicitHeight: Theme.px(30)
            color: Theme.surface
            radius: Theme.px(6)
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.px(10)
                anchors.rightMargin: Theme.px(4)
                Icon { name: composer.editingId.length > 0 ? "edit" : "reply"; size: Theme.px(14) }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    color: Theme.textMuted
                    font.pixelSize: Theme.px(12)
                    text: composer.editingId.length > 0 ? qsTr("Editing message — Enter to save, Esc to cancel")
                                                        : qsTr("Replying to %1").arg(App.replyToPreview)
                }
                IconButton {
                    implicitWidth: Theme.px(24)
                    implicitHeight: Theme.px(24)
                    iconSize: Theme.px(14)
                    iconName: "x"
                    tip: qsTr("Cancel")
                    onClicked: composer.editingId.length > 0 ? composer.finishEdit() : App.cancelReply()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Math.min(input.implicitHeight, Theme.px(220)) + Theme.px(4)
            radius: Theme.px(8)
            color: Theme.surfaceAlt
            border.width: input.activeFocus ? 1 : 0
            border.color: Theme.border

            ScrollView {
                id: scroll
                anchors.fill: parent
                anchors.rightMargin: Theme.px(36)
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                TextArea {
                    id: input
                    enabled: App.canSend || composer.editingId.length > 0
                    wrapMode: TextArea.Wrap
                    color: Theme.text
                    placeholderText: !App.canSend ? qsTr("You do not have permission to send messages here")
                                   : App.homeSelected ? qsTr("Message @%1").arg(App.selectedChannelName)
                                   : qsTr("Message #%1").arg(App.selectedChannelName)
                    placeholderTextColor: Theme.textFaint
                    selectionColor: Theme.accent
                    selectedTextColor: Theme.accentText
                    font.pixelSize: Theme.px(14)
                    leftPadding: Theme.px(12)
                    topPadding: Theme.px(10)
                    bottomPadding: Theme.px(10)
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
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: Theme.px(4)
                iconName: "plus"
                tip: qsTr("Attach a file")
                enabled: App.canSend
                onClicked: App.showAttachmentNotice()
            }

            // Drag & drop: attachments arrive in OmaChat 0.2; say so plainly.
            DropArea {
                anchors.fill: parent
                onEntered: drag => drag.accept(Qt.CopyAction)
                onDropped: drop => App.showAttachmentNotice()
                Rectangle {
                    anchors.fill: parent
                    visible: parent.containsDrag
                    radius: Theme.px(8)
                    color: "transparent"
                    border.color: Theme.accent
                    border.width: 2
                }
            }
        }
    }
}
