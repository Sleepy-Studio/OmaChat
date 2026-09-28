import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// Untrusted TLS certificate: never accepted silently. The user must compare
// and explicitly trust the exact SHA-256 fingerprint the server presented.
Rectangle {
    color: Theme.background

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.px(32), Theme.px(560))
        height: body.implicitHeight + Theme.px(48)
        radius: Theme.px(8)
        color: Theme.surface
        border.color: Theme.danger

        ColumnLayout {
            id: body
            anchors.fill: parent
            anchors.margins: Theme.px(24)
            spacing: Theme.px(12)

            RowLayout {
                spacing: Theme.px(10)
                Icon { name: "shield"; size: Theme.px(28); color: Theme.danger }
                Text {
                    text: qsTr("Untrusted server certificate")
                    color: Theme.text
                    font.pixelSize: Theme.px(18)
                    font.bold: true
                }
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textMuted
                font.pixelSize: Theme.px(13)
                text: qsTr("%1:%2 presented a certificate that is not signed by an authority this system trusts. "
                           + "This is normal for self-hosted servers with a self-signed certificate — but only trust it "
                           + "if the fingerprint below matches the one your server administrator gave you.")
                      .arg(App.accountHost).arg(App.accountPort)
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textFaint
                font.pixelSize: Theme.px(12)
                text: App.errorMessage
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: fp.implicitHeight + Theme.px(16)
                radius: Theme.px(4)
                color: Theme.codeBackground
                TextEdit {
                    id: fp
                    anchors.fill: parent
                    anchors.margins: Theme.px(8)
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    text: App.certificateFingerprint
                    color: Theme.text
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.px(12)
                    Accessible.name: qsTr("Certificate fingerprint")
                }
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textFaint
                font.pixelSize: Theme.px(11)
                text: qsTr("The server administrator can print it with: omachat-server generate-cert … or from the server log line “tls identity loaded”.")
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.px(8)
                FlatButton {
                    text: qsTr("Use a different server")
                    onClicked: App.logout()
                }
                FlatButton {
                    text: qsTr("Copy fingerprint")
                    onClicked: App.copyText(App.certificateFingerprint)
                }
                FlatButton {
                    primary: true
                    danger: true
                    text: qsTr("Trust this certificate")
                    onClicked: App.trustCertificate()
                }
            }
        }
    }
}
