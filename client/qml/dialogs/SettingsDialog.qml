import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// Settings are applied and persisted by omachatd (config.toml).
Dialog {
    id: dialog
    title: qsTr("Settings")
    width: Math.min(Theme.px(640), (parent ? parent.width : 800) - Theme.px(40))
    height: Math.min(Theme.px(560), (parent ? parent.height : 600) - Theme.px(40))

    onAboutToShow: {
        App.refreshAudio()
        App.refreshOAuthIdentities()
    }

    component Row2: RowLayout {
        property alias label: lbl.text
        Layout.fillWidth: true
        spacing: Theme.px(12)
        Text {
            id: lbl
            Layout.preferredWidth: Theme.px(170)
            color: Theme.text
            font.pixelSize: Theme.px(13)
            wrapMode: Text.Wrap
        }
    }

    component Combo: ComboBox {
        id: combo
        Layout.fillWidth: true
        implicitHeight: Theme.px(32)
        textRole: "name"
        valueRole: "id"
        font.pixelSize: Theme.px(13)
        palette.button: Theme.surfaceAlt
        palette.buttonText: Theme.text
        palette.window: Theme.raised
        palette.text: Theme.text
        palette.highlight: Theme.selection
        palette.highlightedText: Theme.text
    }

    component Toggle: Switch {
        font.pixelSize: Theme.px(13)
        palette.base: Theme.surfaceAlt
        contentItem: Text {
            leftPadding: parent.indicator.width + Theme.px(8)
            text: parent.text
            color: Theme.text
            font: parent.font
            verticalAlignment: Text.AlignVCenter
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.px(10)

        RowLayout {
            Text { text: dialog.title; color: Theme.text; font.pixelSize: Theme.px(16); font.bold: true; Layout.fillWidth: true }
            IconButton { iconName: "x"; tip: qsTr("Close"); onClicked: dialog.close() }
        }

        TabBar {
            id: tabs
            Layout.fillWidth: true
            background: Rectangle { color: "transparent" }
            Repeater {
                model: [qsTr("Voice & Audio"), qsTr("Screen sharing"), qsTr("Notifications"), qsTr("Account")]
                delegate: TabButton {
                    required property string modelData
                    text: modelData
                    font.pixelSize: Theme.px(13)
                    contentItem: Text {
                        text: parent.text
                        font: parent.font
                        color: parent.checked ? Theme.text : Theme.textMuted
                        horizontalAlignment: Text.AlignHCenter
                    }
                    background: Rectangle {
                        color: "transparent"
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 2
                            color: parent.parent.checked ? Theme.accent : Theme.border
                        }
                    }
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // ---------------------------------------------------- audio
            ScrollView {
                clip: true
                ColumnLayout {
                    width: dialog.availableWidth - Theme.px(12)
                    spacing: Theme.px(12)

                    Row2 {
                        label: qsTr("Input device")
                        Combo {
                            model: App.inputDevices
                            currentIndex: Math.max(0, indexOfValue(App.audioSettings.input))
                            onActivated: App.setAudio("input", currentValue)
                            Accessible.name: qsTr("Input device")
                        }
                    }
                    Row2 {
                        label: qsTr("Output device")
                        Combo {
                            model: App.outputDevices
                            currentIndex: Math.max(0, indexOfValue(App.audioSettings.output))
                            onActivated: App.setAudio("output", currentValue)
                            Accessible.name: qsTr("Output device")
                        }
                    }
                    Row2 {
                        label: qsTr("Input mode")
                        Combo {
                            model: [
                                { id: "vad", name: qsTr("Voice activity") },
                                { id: "ptt", name: qsTr("Push to talk") },
                                { id: "always", name: qsTr("Always transmit") }
                            ]
                            currentIndex: Math.max(0, indexOfValue(App.inputMode))
                            onActivated: App.setInputMode(currentValue)
                            Accessible.name: qsTr("Input mode")
                        }
                    }
                    Text {
                        visible: App.inputMode === "ptt"
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.textFaint
                        font.pixelSize: Theme.px(11)
                        text: qsTr("In this window, hold %1. For a system-wide key, add to your Hyprland config:\n"
                                   + "bind = , F8, exec, omachatctl ptt begin\nbindr = , F8, exec, omachatctl ptt end")
                              .arg(App.shortcuts["push_to_talk"] || "F8")
                        textFormat: Text.PlainText
                        font.family: Theme.monoFamily
                    }
                    Row2 {
                        label: qsTr("Voice activity threshold")
                        Slider {
                            id: vad
                            Layout.fillWidth: true
                            from: -80
                            to: -20
                            stepSize: 1
                            value: App.audioSettings.vad_threshold_db !== undefined ? App.audioSettings.vad_threshold_db : -50
                            onMoved: App.setAudio("vad_threshold_db", value)
                            Accessible.name: qsTr("Voice activity threshold")
                        }
                        Text { text: Math.round(vad.value) + " dB"; color: Theme.textMuted; font.pixelSize: Theme.px(12) }
                    }
                    Row2 {
                        label: qsTr("Input volume")
                        Slider {
                            id: inVol
                            Layout.fillWidth: true
                            from: 0
                            to: 200
                            stepSize: 5
                            value: (App.audioSettings.input_volume !== undefined ? App.audioSettings.input_volume : 1) * 100
                            onMoved: App.setAudio("input_volume", value / 100)
                            Accessible.name: qsTr("Input volume")
                        }
                        Text { text: Math.round(inVol.value) + "%"; color: Theme.textMuted; font.pixelSize: Theme.px(12) }
                    }
                    Row2 {
                        label: qsTr("Output volume")
                        Slider {
                            id: outVol
                            Layout.fillWidth: true
                            from: 0
                            to: 200
                            stepSize: 5
                            value: (App.audioSettings.output_volume !== undefined ? App.audioSettings.output_volume : 1) * 100
                            onMoved: App.setAudio("output_volume", value / 100)
                            Accessible.name: qsTr("Output volume")
                        }
                        Text { text: Math.round(outVol.value) + "%"; color: Theme.textMuted; font.pixelSize: Theme.px(12) }
                    }
                    Row2 {
                        label: qsTr("Voice bitrate")
                        Combo {
                            model: [
                                { id: 24000, name: "24 kbps" }, { id: 32000, name: "32 kbps" },
                                { id: 40000, name: qsTr("40 kbps (default)") }, { id: 64000, name: "64 kbps" },
                                { id: 96000, name: "96 kbps" }
                            ]
                            currentIndex: Math.max(0, indexOfValue(App.audioSettings.bitrate))
                            onActivated: App.setAudio("bitrate", currentValue)
                            Accessible.name: qsTr("Voice bitrate")
                        }
                    }
                    Toggle {
                        text: App.audioSettings.noise_suppression_available ? qsTr("Noise suppression (RNNoise)")
                                                                            : qsTr("Noise suppression (not available in this build)")
                        enabled: App.audioSettings.noise_suppression_available === true
                        checked: App.audioSettings.noise_suppression === true
                        onToggled: App.setAudio("noise_suppression", checked)
                    }
                    Toggle {
                        text: qsTr("High-pass filter (removes rumble)")
                        checked: App.audioSettings.high_pass === true
                        onToggled: App.setAudio("high_pass", checked)
                    }
                    Toggle {
                        text: qsTr("Automatic gain control")
                        checked: App.audioSettings.automatic_gain === true
                        onToggled: App.setAudio("automatic_gain", checked)
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.textFaint
                        font.pixelSize: Theme.px(11)
                        text: qsTr("Echo cancellation: use PipeWire's echo-cancel module and select its source above.")
                    }
                }
            }

            // ------------------------------------------ screen sharing
            ColumnLayout {
                spacing: Theme.px(12)
                Row2 {
                    label: qsTr("Resolution")
                    Combo {
                        model: [{ id: 720, name: "720p" }, { id: 1080, name: qsTr("1080p (default)") }, { id: 1440, name: "1440p" }]
                        currentIndex: Math.max(0, indexOfValue(App.videoSettings.max_height))
                        onActivated: App.setVideo("max_height", currentValue)
                        Accessible.name: qsTr("Resolution")
                    }
                }
                Row2 {
                    label: qsTr("Frame rate")
                    Combo {
                        model: [{ id: 15, name: "15 fps" }, { id: 30, name: qsTr("30 fps (default)") }, { id: 60, name: "60 fps" }]
                        currentIndex: Math.max(0, indexOfValue(App.videoSettings.fps))
                        onActivated: App.setVideo("fps", currentValue)
                        Accessible.name: qsTr("Frame rate")
                    }
                }
                Row2 {
                    label: qsTr("Bitrate")
                    Combo {
                        model: [{ id: 1500, name: "1.5 Mbps" }, { id: 2500, name: "2.5 Mbps" },
                                { id: 4000, name: qsTr("4 Mbps (default)") }, { id: 6000, name: "6 Mbps" },
                                { id: 10000, name: "10 Mbps" }]
                        currentIndex: Math.max(0, indexOfValue(App.videoSettings.bitrate_kbps))
                        onActivated: App.setVideo("bitrate_kbps", currentValue)
                        Accessible.name: qsTr("Bitrate")
                    }
                }
                Row2 {
                    label: qsTr("Encoder")
                    Combo {
                        readonly property var names: ({ nvenc: "NVIDIA NVENC", amf: "AMD AMF", x264: qsTr("x264 (CPU)") })
                        model: [{ id: "auto", name: qsTr("Automatic") }].concat(
                                   (App.videoSettings.encoders || []).map(e => ({ id: e, name: names[e] || e })))
                        currentIndex: Math.max(0, indexOfValue(App.videoSettings.encoder))
                        onActivated: App.setVideo("encoder", currentValue)
                        Accessible.name: qsTr("Encoder")
                    }
                }
                Toggle {
                    text: qsTr("Share sound from other applications")
                    checked: App.videoSettings.audio !== false
                    onToggled: App.setVideo("audio", checked)
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.textFaint
                    font.pixelSize: Theme.px(11)
                    text: qsTr("Sound comes from every other program's playback, never from OmaChat itself, so the "
                               + "call is not echoed back. Changes apply the next time you share. Automatic tries your GPU first and falls back to the CPU. "
                               + "Only the people in your voice channel who choose to watch receive your screen.")
                }
                Item { Layout.fillHeight: true }
            }

            // ------------------------------------------- notifications
            ColumnLayout {
                spacing: Theme.px(10)
                Toggle {
                    text: qsTr("Direct messages")
                    checked: App.notificationSettings.messages === true
                    onToggled: App.setNotification("messages", checked)
                }
                Toggle {
                    text: qsTr("Mentions (@you)")
                    checked: App.notificationSettings.mentions === true
                    onToggled: App.setNotification("mentions", checked)
                }
                Toggle {
                    text: qsTr("Someone joins your voice channel")
                    checked: App.notificationSettings.voice_join === true
                    onToggled: App.setNotification("voice_join", checked)
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.textFaint
                    font.pixelSize: Theme.px(11)
                    text: qsTr("Notifications are silenced while your status is Do not disturb, for muted channels, "
                               + "and for the channel you are currently reading.")
                }
                Item { Layout.fillHeight: true }
            }

            // -------------------------------------------------- account
            ColumnLayout {
                spacing: Theme.px(10)
                Text {
                    text: qsTr("Signed in as %1 (@%2)").arg(App.selfName).arg(App.selfUsername)
                    color: Theme.text
                    font.pixelSize: Theme.px(13)
                }
                Text {
                    text: qsTr("Server: %1:%2 · %3").arg(App.accountHost).arg(App.accountPort).arg(App.instanceName)
                    color: Theme.textMuted
                    font.pixelSize: Theme.px(12)
                }
                Text {
                    text: qsTr("OmaChat %1").arg(App.version)
                    color: Theme.textFaint
                    font.pixelSize: Theme.px(11)
                }

                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

                Text {
                    text: qsTr("Sign-in methods")
                    color: Theme.text
                    font.pixelSize: Theme.px(13)
                    font.bold: true
                }

                Repeater {
                    // Google is temporarily left out here too (see
                    // LoginPage.qml); add {key: "google", label: qsTr("Google")}
                    // back once it's re-enabled.
                    model: [
                        {key: "discord", label: qsTr("Discord")},
                        {key: "github", label: qsTr("GitHub")},
                    ]
                    delegate: RowLayout {
                        required property var modelData
                        readonly property var identity: App.oauthIdentities.find(i => i.provider === modelData.key)
                        Layout.fillWidth: true
                        spacing: Theme.px(8)
                        Text {
                            Layout.fillWidth: true
                            color: Theme.text
                            font.pixelSize: Theme.px(13)
                            text: identity ? qsTr("%1 — linked as %2").arg(modelData.label).arg(identity.username)
                                           : modelData.label
                        }
                        FlatButton {
                            enabled: !App.oauthLinkBusy
                            danger: !!identity
                            text: identity ? qsTr("Unlink")
                                           : (App.oauthLinkBusy ? qsTr("Waiting on browser…") : qsTr("Link"))
                            onClicked: identity ? App.unlinkOAuthProvider(modelData.key)
                                                 : App.linkOAuthProvider(modelData.key)
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.textFaint
                    font.pixelSize: Theme.px(11)
                    text: qsTr("Linking opens your browser to sign in with that provider, then confirms it here. "
                               + "You can't unlink your last sign-in method without a password set.")
                }

                RowLayout {
                    spacing: Theme.px(8)
                    FlatButton {
                        danger: true
                        text: qsTr("Log out")
                        onClicked: {
                            dialog.close()
                            App.logout()
                        }
                    }
                    FlatButton {
                        danger: true
                        text: qsTr("Remove this account")
                        onClicked: {
                            removeAccountConfirm.open()
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }
    }

    ConfirmDialog {
        id: removeAccountConfirm
        title: qsTr("Remove this account?")
        message: qsTr("%1@%2 will be forgotten on this device, including its saved sign-in. You can add it "
                      + "again with its password, or by signing in with its linked provider.")
                      .arg(App.accountUser).arg(App.accountHost)
        confirmText: qsTr("Remove")
        destructive: true
        onConfirmed: {
            dialog.close()
            App.removeAccount(App.accountId)
        }
    }
}
