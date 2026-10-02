import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// End-to-end encryption of the selected conversation. Each person's safety
// number is the same on both of your screens only if nobody (including the
// server) slipped a key of their own into the conversation.
Dialog {
    id: dialog
    title: qsTr("Encryption")
    width: Math.min(Metrics.px(520), (parent ? parent.width : 800) - Metrics.px(40))

    onAboutToShow: App.loadSafetyNumbers()

    contentItem: ColumnLayout {
        spacing: Metrics.px(12)

        RowLayout {
            Icon { name: "lock"; size: Metrics.px(18); color: Theme.success }
            Text { text: dialog.title; color: Theme.text; font.pixelSize: Metrics.px(16); font.bold: true; Layout.fillWidth: true }
            IconButton { iconName: "x"; tip: qsTr("Close"); onClicked: dialog.close() }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.textMuted
            font.pixelSize: Metrics.px(12)
            text: qsTr("Messages and files in this conversation are encrypted on your device and can only be read "
                       + "by the devices of the people in it. The server cannot read them. Compare each number below "
                       + "with that person in person or on a call; if they match, mark them verified.")
        }
        Repeater {
            model: App.safetyNumbers
            delegate: Rectangle {
                id: entry
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: col.implicitHeight + Metrics.px(16)
                radius: Metrics.px(6)
                color: Theme.surfaceAlt
                ColumnLayout {
                    id: col
                    anchors.fill: parent
                    anchors.margins: Metrics.px(8)
                    spacing: Metrics.px(6)
                    RowLayout {
                        Text {
                            Layout.fillWidth: true
                            text: entry.modelData.name
                            color: Theme.text
                            font.bold: true
                            font.pixelSize: Metrics.px(13)
                        }
                        Text {
                            text: qsTr("%n device(s)", "", entry.modelData.devices)
                            color: Theme.textFaint
                            font.pixelSize: Metrics.px(11)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: {
                            const g = (entry.modelData.number || "").split(" ")
                            return [g.slice(0, 4).join("  "), g.slice(4, 8).join("  "), g.slice(8, 12).join("  ")].join("\n")
                        }
                        color: Theme.text
                        font.family: Theme.monoFamily
                        font.pixelSize: Metrics.px(15)
                        horizontalAlignment: Text.AlignHCenter
                        textFormat: Text.PlainText
                        Accessible.name: qsTr("Safety number with %1: %2").arg(entry.modelData.name).arg(entry.modelData.number)
                    }
                    RowLayout {
                        Text {
                            Layout.fillWidth: true
                            text: entry.modelData.verified ? qsTr("✓ Verified") : qsTr("Not verified")
                            color: entry.modelData.verified ? Theme.success : Theme.textMuted
                            font.pixelSize: Metrics.px(12)
                        }
                        FlatButton {
                            text: entry.modelData.verified ? qsTr("Clear verification") : qsTr("Mark as verified")
                            primary: !entry.modelData.verified
                            onClicked: App.setVerified(entry.modelData.userId, !entry.modelData.verified)
                        }
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.textFaint
            font.pixelSize: Metrics.px(11)
            text: qsTr("A number changes when someone adds or removes a device; OmaChat warns you when that happens. "
                       + "Voice, video and server channels are encrypted in transit but not end to end.")
        }
    }
}
