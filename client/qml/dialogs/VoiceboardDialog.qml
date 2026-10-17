import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// The existing voiceboard entry points and controller require this component.
// Audio is sent only after an explicit Speak action.
Dialog {
    id: dialog
    title: qsTr("Voiceboard")
    width: Math.min(Metrics.px(460), (parent ? parent.width : 800) - Metrics.px(32))
    height: Math.min(Metrics.px(380), (parent ? parent.height : 600) - Metrics.px(32))
    onOpened: message.forceActiveFocus()
    contentItem: ColumnLayout {
        spacing: Metrics.px(10)
        Text {
            Layout.fillWidth: true
            text: dialog.title
            color: Theme.text
            font.pixelSize: Metrics.px(16)
            font.bold: true
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Press Speak to send this text as audio into your current voice channel.")
            wrapMode: Text.Wrap
            color: Theme.textMuted
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            TextArea {
                id: message
                placeholderText: qsTr("What would you like to say?")
                Accessible.name: qsTr("Text to speak into voice")
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.Wrap
                color: Theme.text
                enabled: !App.voiceboardBusy
            }
        }
        Check {
            id: monitor
            text: qsTr("Also play locally")
            checked: false
            enabled: !App.voiceboardBusy
        }
        Text {
            Layout.fillWidth: true
            text: !App.voiceJoined ? qsTr("Join voice before speaking.") : App.voiceboardStatus
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: Theme.textMuted
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            FlatButton { text: qsTr("Close"); onClicked: dialog.close() }
            FlatButton {
                text: App.voiceboardBusy ? qsTr("Sending…") : qsTr("Speak")
                enabled: App.voiceJoined && !App.voiceboardBusy && message.text.trim().length > 0
                onClicked: App.speakVoiceboard(message.text, monitor.checked)
            }
        }
    }
}
