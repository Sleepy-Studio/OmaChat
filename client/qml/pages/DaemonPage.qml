import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// Shown until the GUI reaches omachatd: starting, launching the user
// service, reconnecting after a daemon restart, or an explicit failure.
Rectangle {
    color: Theme.background

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.px(48), Theme.px(460))
        spacing: Theme.px(14)

        Icon {
            Layout.alignment: Qt.AlignHCenter
            name: App.daemonState === "unavailable" ? "alert" : "server"
            size: Theme.px(40)
            color: App.daemonState === "unavailable" ? Theme.danger : Theme.textMuted
        }

        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            color: Theme.text
            font.pixelSize: Theme.px(18)
            font.bold: true
            text: App.daemonState === "unavailable" ? qsTr("OmaChat service unavailable")
                : App.daemonState === "launching" ? qsTr("Starting the OmaChat service…")
                : App.daemonState === "reconnecting" ? qsTr("Reconnecting to the OmaChat service…")
                : qsTr("Starting…")
        }

        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: Theme.textMuted
            font.pixelSize: Theme.px(13)
            text: App.daemonState === "unavailable" ? App.daemonError
                : qsTr("omachatd keeps your connection and voice alive even when this window is closed.")
        }

        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            visible: App.daemonState !== "unavailable"
            running: visible
            implicitWidth: Theme.px(28)
            implicitHeight: Theme.px(28)
        }

        FlatButton {
            Layout.alignment: Qt.AlignHCenter
            visible: App.daemonState === "unavailable"
            primary: true
            text: qsTr("Try again")
            onClicked: App.retryDaemon()
        }
    }
}
