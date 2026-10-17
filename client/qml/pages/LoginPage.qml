import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// Account setup: log in to or register on a self-hosted server. The
// password is handed to omachatd and cleared from the field immediately.
Rectangle {
    id: page
    color: Theme.background

    property bool registering: false

    function submit() {
        if (App.authBusy)
            return
        const port = parseInt(portField.text)
        App.login(hostField.text, isNaN(port) ? 6473 : port, userField.text, passwordField.text,
                  page.registering, displayField.text)
        passwordField.text = ""
    }

    Component.onCompleted: {
        if (App.accountHost.length > 0 && !App.addingAccount) {
            hostField.text = App.accountHost
            portField.text = App.accountPort
            userField.text = App.accountUser
            passwordField.input.forceActiveFocus()
        } else {
            hostField.input.forceActiveFocus()
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - Metrics.px(32), Metrics.px(420))
        height: form.implicitHeight + Metrics.px(48)
        radius: Metrics.px(8)
        color: Theme.surface
        border.color: Theme.border

        ColumnLayout {
            id: form
            anchors.fill: parent
            anchors.margins: Metrics.px(24)
            spacing: Metrics.px(14)

            RowLayout {
                spacing: Metrics.px(10)
                Image {
                    source: "qrc:/qt/qml/OmaChat/icons/app.svg"
                    sourceSize: Qt.size(Metrics.px(36), Metrics.px(36))
                }
                ColumnLayout {
                    spacing: 0
                    Text {
                        text: page.registering ? qsTr("Create an account") : qsTr("Welcome to OmaChat")
                        color: Theme.text
                        font.pixelSize: Metrics.px(18)
                        font.bold: true
                    }
                    Text {
                        text: page.registering ? qsTr("Register on a self-hosted OmaChat server")
                                               : qsTr("Log in to your OmaChat server")
                        color: Theme.textMuted
                        font.pixelSize: Metrics.px(12)
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Metrics.px(8)
                Field {
                    id: hostField
                    Layout.fillWidth: true
                    label: qsTr("Server")
                    placeholder: "chat.example.org"
                    input.onAccepted: userField.input.forceActiveFocus()
                }
                Field {
                    id: portField
                    Layout.preferredWidth: Metrics.px(80)
                    label: qsTr("Port")
                    placeholder: "6473"
                    input.validator: IntValidator { bottom: 1; top: 65535 }
                }
            }

            Field {
                id: userField
                Layout.fillWidth: true
                label: qsTr("Username")
                hint: page.registering ? qsTr("2–32 characters: a–z, 0–9, '.', '_', '-'") : ""
                input.onAccepted: (page.registering ? displayField : passwordField).input.forceActiveFocus()
            }

            Field {
                id: displayField
                visible: page.registering
                Layout.fillWidth: true
                label: qsTr("Display name")
                placeholder: qsTr("Optional")
                input.onAccepted: passwordField.input.forceActiveFocus()
            }

            Field {
                id: passwordField
                Layout.fillWidth: true
                label: qsTr("Password")
                echoMode: TextInput.Password
                hint: page.registering ? qsTr("At least 8 characters.") : ""
                input.onAccepted: page.submit()
            }

            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                wrapMode: Text.Wrap
                color: Theme.danger
                font.pixelSize: Metrics.px(12)
                text: App.authError.length > 0 ? App.authError
                    : (App.state === "error" && App.errorCode !== "CertificateError" ? App.errorMessage : "")
                Accessible.role: Accessible.AlertMessage
                Accessible.name: text
            }

            FlatButton {
                Layout.fillWidth: true
                primary: true
                enabled: !App.authBusy
                text: App.authBusy ? qsTr("Connecting…") : (page.registering ? qsTr("Create account") : qsTr("Log in"))
                onClicked: page.submit()
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Metrics.px(8)
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
                Text { text: qsTr("or"); color: Theme.textMuted; font.pixelSize: Metrics.px(11) }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
            }

            // The server may not have every provider enabled; if not, it
            // replies with a clear error shown above like any other one.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Metrics.px(8)

                function go(provider) {
                    const port = parseInt(portField.text)
                    App.loginWithOAuth(hostField.text, isNaN(port) ? 6473 : port, provider)
                }

                FlatButton {
                    Layout.fillWidth: true
                    enabled: !App.authBusy
                    text: qsTr("Continue with Discord")
                    onClicked: parent.go("discord")
                }
                FlatButton {
                    Layout.fillWidth: true
                    enabled: !App.authBusy
                    text: qsTr("Continue with GitHub")
                    onClicked: parent.go("github")
                }
                // Google sign-in is temporarily disabled (its setup isn't
                // ready yet); flip this back to visible: true to bring it
                // back. Server/daemon support is untouched.
                FlatButton {
                    visible: false
                    Layout.fillWidth: true
                    enabled: !App.authBusy
                    text: qsTr("Continue with Google")
                    onClicked: parent.go("google")
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Text {
                    text: page.registering ? qsTr("Already have an account?") : qsTr("New to this server?")
                    color: Theme.textMuted
                    font.pixelSize: Metrics.px(12)
                }
                Text {
                    text: page.registering ? qsTr("Log in") : qsTr("Register")
                    color: Theme.accent
                    font.pixelSize: Metrics.px(12)
                    font.underline: toggleArea.containsMouse
                    MouseArea {
                        id: toggleArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.registering = !page.registering
                    }
                    Accessible.role: Accessible.Link
                    Accessible.name: text
                }
            }

            // Other saved accounts: go back instead of logging in here.
            ColumnLayout {
                readonly property var others: App.accounts.filter(a => !a.active)
                visible: App.addingAccount || others.length > 0
                Layout.fillWidth: true
                spacing: Metrics.px(6)
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
                Repeater {
                    model: parent.others
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: Metrics.px(4)
                        FlatButton {
                            Layout.fillWidth: true
                            text: qsTr("Use %1@%2").arg(modelData.username).arg(modelData.host)
                            onClicked: App.switchAccount(modelData.id)
                        }
                        IconButton {
                            iconName: "trash"
                            danger: true
                            tip: qsTr("Delete this account from the server")
                            onClicked: {
                                removeConfirm.accountId = modelData.id
                                removeConfirm.accountLabel = modelData.username + "@" + modelData.host
                                removeConfirm.open()
                            }
                        }
                    }
                }
                FlatButton {
                    visible: App.addingAccount
                    Layout.fillWidth: true
                    text: qsTr("Cancel")
                    onClicked: App.addingAccount = false
                }
            }
        }
    }

    ConfirmDialog {
        id: removeConfirm
        property string accountId
        property string accountLabel
        title: qsTr("Permanently delete account?")
        message: qsTr("%1 will be deleted from the server and this device. Owned servers and private "
                      + "conversations will also be deleted. This cannot be undone. The account must be connected.").arg(accountLabel)
        confirmText: qsTr("Delete account")
        destructive: true
        onConfirmed: App.removeAccount(accountId)
    }
}
