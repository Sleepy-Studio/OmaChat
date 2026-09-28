import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

ApplicationWindow {
    id: window

    width: 1200
    height: 760
    minimumWidth: 720
    minimumHeight: 460
    visible: true
    title: App.ready && App.selectedChannelName.length > 0
           ? (App.homeSelected ? "@" : "#") + App.selectedChannelName + " — OmaChat"
           : "OmaChat"
    color: Theme.background

    font.family: Theme.fontFamily
    font.pixelSize: Theme.px(14)

    // Palette for stock controls (ToolTip, BusyIndicator, ScrollBar, ...).
    palette.window: Theme.background
    palette.windowText: Theme.text
    palette.base: Theme.surfaceAlt
    palette.text: Theme.text
    palette.button: Theme.surfaceAlt
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentText
    palette.toolTipBase: Theme.raised
    palette.toolTipText: Theme.text
    palette.mid: Theme.border
    palette.dark: Theme.textFaint

    onActiveChanged: App.setWindowFocused(active)

    readonly property string page: {
        if (App.daemonState !== "connected")
            return "daemon"
        if (App.state === "error" && App.errorCode === "CertificateError")
            return "certificate"
        if (App.ready)
            return "main"
        if (App.state === "not_configured" || App.state === "login_required" || App.state === "error")
            return "login"
        return "connecting"
    }

    Loader {
        anchors.fill: parent
        sourceComponent: window.page === "daemon" ? daemonPage
                       : window.page === "certificate" ? certificatePage
                       : window.page === "login" ? loginPage
                       : window.page === "main" ? mainView
                       : connectingPage
    }

    Component { id: daemonPage; DaemonPage {} }
    Component { id: certificatePage; CertificatePage {} }
    Component { id: loginPage; LoginPage {} }
    Component { id: connectingPage; ConnectingPage {} }
    Component { id: mainView; MainView {} }

    // Transient notices (errors stay longer; click to dismiss).
    Rectangle {
        id: toast
        visible: App.notice.length > 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.px(72)
        width: Math.min(noticeText.implicitWidth + Theme.px(28), parent.width - Theme.px(40))
        height: noticeText.implicitHeight + Theme.px(16)
        radius: Theme.px(6)
        color: Theme.raised
        border.color: App.noticeIsError ? Theme.danger : Theme.border
        z: 100

        Accessible.role: Accessible.AlertMessage
        Accessible.name: App.notice

        Text {
            id: noticeText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, window.width - Theme.px(68))
            text: App.notice
            wrapMode: Text.Wrap
            color: App.noticeIsError ? Theme.danger : Theme.text
            font.pixelSize: Theme.px(13)
        }
        MouseArea {
            anchors.fill: parent
            onClicked: App.dismissNotice()
        }
    }

    // omachat://invite links (from messages or the desktop) always ask first.
    ConfirmDialog {
        id: inviteConfirm
        property string invite
        title: qsTr("Join server?")
        message: qsTr("Accept the invite %1?").arg(invite)
        confirmText: qsTr("Join")
        onConfirmed: App.joinServer(invite)
    }
    Connections {
        target: App
        function onRequestInviteJoin(invite) {
            inviteConfirm.invite = invite
            inviteConfirm.open()
        }
    }
}
