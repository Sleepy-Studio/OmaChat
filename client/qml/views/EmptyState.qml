import QtQuick
import QtQuick.Layouts
import OmaChat

ColumnLayout {
    property string icon: "message"
    property string title
    property string subtitle

    spacing: Theme.px(8)
    width: Math.min(parent ? parent.width - Theme.px(48) : Theme.px(400), Theme.px(420))

    Icon {
        Layout.alignment: Qt.AlignHCenter
        name: parent.icon
        size: Theme.px(36)
        color: Theme.textFaint
    }
    Text {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        text: parent.title
        color: Theme.text
        font.pixelSize: Theme.px(16)
        font.bold: true
        wrapMode: Text.Wrap
    }
    Text {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        text: parent.subtitle
        color: Theme.textMuted
        font.pixelSize: Theme.px(13)
        wrapMode: Text.Wrap
    }
}
