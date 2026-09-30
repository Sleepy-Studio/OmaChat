import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import OmaChat

Dialog {
    id: dialog
    title: qsTr("Create a server")
    closePolicy: App.discordImportBusy ? Popup.NoAutoClose
        : Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property bool importing: false
    property var exportFiles: []

    onAboutToShow: {
        if (!App.discordImportBusy) {
            nameField.text = ""
            descriptionField.text = ""
            importing = false
            exportFiles = []
        }
    }
    onOpened: nameField.input.forceActiveFocus()

    FileDialog {
        id: exportPicker
        title: qsTr("Choose Discord JSON channel exports")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("JSON exports (*.json)")]
        onAccepted: dialog.exportFiles = selectedFiles
    }

    Connections {
        target: App
        function onDiscordImportFinished(success) {
            if (success)
                dialog.close()
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.px(14)

        Text {
            text: dialog.title
            color: Theme.text
            font.pixelSize: Theme.px(16)
            font.bold: true
        }

        Field {
            id: nameField
            Layout.fillWidth: true
            label: qsTr("Server name")
            placeholder: qsTr("Sleepy Studio")
            input.enabled: !App.discordImportBusy
            input.onAccepted: createButton.clicked()
        }

        Field {
            id: descriptionField
            Layout.fillWidth: true
            label: qsTr("Description (optional)")
            placeholder: qsTr("What is this server for?")
            input.enabled: !App.discordImportBusy
            input.maximumLength: 2000
            input.onAccepted: createButton.clicked()
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.px(8)
            FlatButton {
                text: qsTr("Start fresh")
                primary: !dialog.importing
                enabled: !App.discordImportBusy
                onClicked: dialog.importing = false
            }
            FlatButton {
                text: qsTr("Import Discord")
                primary: dialog.importing
                enabled: !App.discordImportBusy
                onClicked: dialog.importing = true
            }
        }

        ColumnLayout {
            visible: dialog.importing
            Layout.fillWidth: true
            spacing: Theme.px(8)

            Text {
                Layout.fillWidth: true
                text: qsTr("Choose JSON channel exports from the same Discord server. Include downloaded assets beside them to bring over attachments.")
                color: Theme.textMuted
                font.pixelSize: Theme.px(12)
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                FlatButton {
                    text: qsTr("Choose export files…")
                    enabled: !App.discordImportBusy
                    onClicked: exportPicker.open()
                }
                Text {
                    text: dialog.exportFiles.length === 0 ? qsTr("No files selected")
                         : qsTr("%1 selected").arg(dialog.exportFiles.length)
                    color: Theme.textMuted
                    font.pixelSize: Theme.px(12)
                }
            }
        }

        Text {
            visible: dialog.importing && App.discordImportStatus.length > 0
            Layout.fillWidth: true
            text: App.discordImportStatus
            color: Theme.textMuted
            font.pixelSize: Theme.px(12)
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.px(8)
            FlatButton {
                text: qsTr("Cancel")
                enabled: !App.discordImportBusy
                onClicked: dialog.close()
            }
            FlatButton {
                id: createButton
                primary: true
                text: App.discordImportBusy ? qsTr("Importing…")
                    : dialog.importing ? qsTr("Create and import") : qsTr("Create")
                enabled: !App.discordImportBusy && nameField.text.trim().length > 0
                    && (!dialog.importing || dialog.exportFiles.length > 0)
                onClicked: {
                    if (dialog.importing)
                        App.createServerFromDiscord(nameField.text.trim(), dialog.exportFiles)
                    else {
                        App.createServer(nameField.text.trim(), descriptionField.text.trim())
                        dialog.close()
                    }
                }
            }
        }
    }
}
