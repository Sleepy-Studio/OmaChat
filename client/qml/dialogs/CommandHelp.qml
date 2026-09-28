import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// Ctrl+/: commands and keyboard shortcuts.
Dialog {
    id: dialog
    title: qsTr("Commands and shortcuts")
    width: Math.min(Theme.px(620), (parent ? parent.width : 800) - Theme.px(40))

    readonly property var shortcutRows: [
        { action: qsTr("Quick switcher"), key: App.shortcuts["quick_switcher"] },
        { action: qsTr("Toggle mute"), key: App.shortcuts["toggle_mute"] },
        { action: qsTr("Toggle deafen"), key: App.shortcuts["toggle_deafen"] },
        { action: qsTr("Previous / next channel"), key: (App.shortcuts["previous_channel"] || "") + " / " + (App.shortcuts["next_channel"] || "") },
        { action: qsTr("Focus channel list"), key: App.shortcuts["focus_channels"] },
        { action: qsTr("Search channel"), key: App.shortcuts["search"] },
        { action: qsTr("Push to talk (in window)"), key: App.shortcuts["push_to_talk"] },
        { action: qsTr("Send / new line"), key: "Enter / Shift+Enter" },
        { action: qsTr("Edit your last message"), key: "Up" },
        { action: qsTr("Complete @user, #channel, /command"), key: "Tab" },
        { action: qsTr("Close dialog, cancel reply or edit"), key: "Esc" }
    ]

    contentItem: ColumnLayout {
        spacing: Theme.px(10)

        Text { text: dialog.title; color: Theme.text; font.pixelSize: Theme.px(16); font.bold: true }

        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(Theme.px(460), content.implicitHeight)
            clip: true

            ColumnLayout {
                id: content
                width: parent.width
                spacing: Theme.px(4)

                SectionLabel { text: qsTr("COMMANDS") }
                Repeater {
                    model: App.commandHelp
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: Theme.px(12)
                        Text {
                            Layout.preferredWidth: Theme.px(200)
                            text: modelData.usage
                            color: Theme.accent
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.px(12)
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.description
                            color: Theme.textMuted
                            font.pixelSize: Theme.px(12)
                            wrapMode: Text.Wrap
                        }
                    }
                }

                SectionLabel { text: qsTr("KEYBOARD"); Layout.topMargin: Theme.px(10) }
                Repeater {
                    model: dialog.shortcutRows
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: Theme.px(12)
                        Text {
                            Layout.preferredWidth: Theme.px(200)
                            text: modelData.key || ""
                            color: Theme.text
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.px(12)
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.action
                            color: Theme.textMuted
                            font.pixelSize: Theme.px(12)
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.px(8)
                    wrapMode: Text.Wrap
                    color: Theme.textFaint
                    font.pixelSize: Theme.px(11)
                    text: qsTr("Shortcuts are configurable in the [shortcuts] section of ~/.config/omachat/config.toml. "
                               + "For a global push-to-talk key, bind `omachatctl ptt begin` / `omachatctl ptt end` in your compositor.")
                }
            }
        }
    }
}
