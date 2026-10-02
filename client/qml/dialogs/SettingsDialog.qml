import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// Settings are applied and persisted by omachatd (config.toml).
Dialog {
    id: dialog
    title: qsTr("Settings")
    width: Math.min(Metrics.px(640), (parent ? parent.width : 800) - Metrics.px(40))
    height: Math.min(Metrics.px(560), (parent ? parent.height : 600) - Metrics.px(40))

    function syncAppearance() {
        if (!App.uiSaving) {
            interfaceScale.currentIndex = interfaceScale.indexOfValue(Theme.scale)
            reduceMotion.checked = Theme.reducedMotion
        }
    }
    Connections {
        target: App
        function onConfigChanged() { dialog.syncAppearance() }
    }
    Connections {
        target: Theme
        function onChanged() { dialog.syncAppearance() }
    }

    onAboutToShow: {
        syncAppearance()
        App.refreshAudio()
        App.refreshOAuthIdentities()
        profileName.text = App.selfName
        profileAvatar.text = App.selfAvatarUrl
        profileBio.text = App.selfBio
    }

    component Row2: RowLayout {
        property alias label: lbl.text
        Layout.fillWidth: true
        spacing: Metrics.px(12)
        Text {
            id: lbl
            Layout.preferredWidth: Metrics.px(170)
            color: Theme.text
            font.pixelSize: Metrics.px(13)
            wrapMode: Text.Wrap
        }
    }

    component Combo: ComboBox {
        id: combo
        Layout.fillWidth: true
        implicitHeight: Metrics.px(32)
        textRole: "name"
        valueRole: "id"
        font.pixelSize: Metrics.px(13)
        palette.button: Theme.surfaceAlt
        palette.buttonText: Theme.text
        palette.window: Theme.raised
        palette.text: Theme.text
        palette.highlight: Theme.selection
        palette.highlightedText: Theme.text
    }

    component Toggle: Switch {
        id: toggle
        font.pixelSize: Metrics.px(13)
        palette.base: Theme.surfaceAlt
        contentItem: Text {
            leftPadding: toggle.indicator.width + Metrics.px(8)
            text: toggle.text
            color: Theme.text
            font: toggle.font
            verticalAlignment: Text.AlignVCenter
        }
    }

    contentItem: ColumnLayout {
        spacing: Metrics.px(10)

        RowLayout {
            Text { text: dialog.title; color: Theme.text; font.pixelSize: Metrics.px(16); font.bold: true; Layout.fillWidth: true }
            IconButton { iconName: "x"; tip: qsTr("Close"); onClicked: dialog.close() }
        }

        Combo {
            id: settingsSection
            visible: dialog.availableWidth < Metrics.px(520)
            model: [
                { id: 0, name: qsTr("Appearance") }, { id: 1, name: qsTr("Voice & Audio") },
                { id: 2, name: qsTr("Screen sharing") }, { id: 3, name: qsTr("Notifications") },
                { id: 4, name: qsTr("Account") }
            ]
            currentIndex: tabs.currentIndex
            onActivated: tabs.currentIndex = currentIndex
            Accessible.name: qsTr("Settings section")
        }

        TabBar {
            id: tabs
            visible: !settingsSection.visible
            Layout.fillWidth: true
            background: Rectangle { color: "transparent" }
            Repeater {
                model: [qsTr("Appearance"), qsTr("Voice & Audio"), qsTr("Screen sharing"), qsTr("Notifications"), qsTr("Account")]
                delegate: TabButton {
                    id: settingsTab
                    required property string modelData
                    text: modelData
                    font.pixelSize: Metrics.px(13)
                    contentItem: Text {
                        text: settingsTab.text
                        font: settingsTab.font
                        color: settingsTab.checked ? Theme.text : Theme.textMuted
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                    }
                    background: Rectangle {
                        color: "transparent"
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 2
                            color: settingsTab.checked ? Theme.accent : Theme.border
                        }
                    }
                }
            }
        }

        StackLayout {
            Layout.minimumHeight: 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // ------------------------------------------------ appearance
            ScrollView {
                Layout.minimumHeight: 0
                clip: true
                ColumnLayout {
                    width: dialog.availableWidth - Metrics.px(12)
                    spacing: Metrics.px(12)
                    Row2 {
                        label: qsTr("Interface scale")
                        Combo {
                            id: interfaceScale
                            objectName: "interfaceScale"
                            enabled: !App.uiSaving
                            model: [
                                { id: 0.75, name: "75%" }, { id: 1.0, name: qsTr("100% (default)") },
                                { id: 1.25, name: "125%" }, { id: 1.5, name: "150%" },
                                { id: 1.75, name: "175%" }, { id: 2.0, name: "200%" },
                                { id: 2.5, name: "250%" }, { id: 3.0, name: "300%" }
                            ]
                            currentIndex: indexOfValue(Theme.scale)
                            displayText: Math.round(Theme.scale * 100) + "%"
                            onActivated: App.setUi(currentValue, Theme.reducedMotion)
                            Accessible.name: qsTr("Interface scale")
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Toggle {
                            id: reduceMotion
                            objectName: "reduceMotion"
                            Layout.fillWidth: true
                            enabled: !App.uiSaving
                            text: qsTr("Reduce motion")
                            checked: Theme.reducedMotion
                            onToggled: App.setUi(Theme.scale, checked)
                            Accessible.name: qsTr("Reduce motion")
                        }
                        FlatButton {
                            enabled: !App.uiSaving && (Theme.scale !== 1.0 || Theme.reducedMotion)
                            objectName: "resetAppearance"
                            text: qsTr("Reset")
                            Accessible.name: qsTr("Reset appearance")
                            onClicked: App.setUi(1.0, false)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.textMuted
                        font.pixelSize: Metrics.px(12)
                        text: qsTr("Text and controls resize immediately. Changes are saved for the next launch. Reduce motion removes interface transitions.")
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: App.uiSaveStatus.length > 0
                        wrapMode: Text.Wrap
                        color: App.uiSaveFailed ? Theme.danger : Theme.textMuted
                        font.pixelSize: Metrics.px(12)
                        text: App.uiSaveStatus
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                    }

                }
            }

            // ---------------------------------------------------- audio
            ScrollView {
                Layout.minimumHeight: 0
                clip: true
                ColumnLayout {
                    width: dialog.availableWidth - Metrics.px(12)
                    spacing: Metrics.px(12)

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
                        color: Theme.textMuted
                        font.pixelSize: Metrics.px(11)
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
                        Text { text: Math.round(vad.value) + " dB"; color: Theme.textMuted; font.pixelSize: Metrics.px(12) }
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
                        Text { text: Math.round(inVol.value) + "%"; color: Theme.textMuted; font.pixelSize: Metrics.px(12) }
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
                        Text { text: Math.round(outVol.value) + "%"; color: Theme.textMuted; font.pixelSize: Metrics.px(12) }
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
                        color: Theme.textMuted
                        font.pixelSize: Metrics.px(11)
                        text: qsTr("Echo cancellation: use PipeWire's echo-cancel module and select its source above.")
                    }
                }
            }

            // ------------------------------------------ screen sharing
            ColumnLayout {
                spacing: Metrics.px(12)
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
                    checked: App.videoSettings.audio === true
                    onToggled: App.setVideo("audio", checked)
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.textMuted
                    font.pixelSize: Metrics.px(11)
                    text: qsTr("Opt in to share sound. It captures every other program's playback, even when you share one window; never OmaChat itself, so the "
                               + "call is not echoed back. Changes apply the next time you share. Automatic tries your GPU first and falls back to the CPU. "
                               + "Only the people in your voice channel who choose to watch receive your screen.")
                }
                Item { Layout.fillHeight: true }
            }

            // ------------------------------------------- notifications
            ColumnLayout {
                spacing: Metrics.px(10)
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
                    color: Theme.textMuted
                    font.pixelSize: Metrics.px(11)
                    text: qsTr("Notifications are silenced while your status is Do not disturb, for muted channels, "
                               + "and for the channel you are currently reading.")
                }
                Item { Layout.fillHeight: true }
            }

            // -------------------------------------------------- account
            ScrollView {
                Layout.minimumHeight: 0
                clip: true
                ColumnLayout {
                width: dialog.availableWidth - Metrics.px(12)
                spacing: Metrics.px(10)
                Text { text: qsTr("Profile"); color: Theme.text; font.pixelSize: Metrics.px(14); font.bold: true }
                RowLayout {
                    Avatar { userId: App.selfId; name: App.selfName; avatarUrl: App.selfAvatarUrl; size: Metrics.px(48) }
                    Text { text: "@" + App.selfUsername; color: Theme.textMuted; font.pixelSize: Metrics.px(12) }
                }
                Field {
                    id: profileName
                    Layout.fillWidth: true
                    label: qsTr("Display name")
                    input.maximumLength: 64
                }
                Field {
                    id: profileAvatar
                    Layout.fillWidth: true
                    label: qsTr("Avatar image URL (HTTPS)")
                    placeholder: qsTr("https://example.com/avatar.png")
                    hint: qsTr("External images are loaded from this address by people who can see your profile.")
                }
                Text { text: qsTr("Bio"); color: Theme.textMuted; font.pixelSize: Metrics.px(11); font.bold: true }
                TextArea {
                    id: profileBio
                    Layout.fillWidth: true
                    Layout.preferredHeight: Metrics.px(76)
                    wrapMode: TextEdit.Wrap
                    color: Theme.text
                    font.pixelSize: Metrics.px(13)
                    placeholderText: qsTr("A little about you")
                    background: Rectangle { color: Theme.surfaceAlt; border.color: Theme.border; radius: Metrics.px(4) }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: profileBio.length + "/300"; color: profileBio.length > 300 ? Theme.danger : Theme.textMuted; font.pixelSize: Metrics.px(11); Layout.fillWidth: true }
                    FlatButton {
                        text: qsTr("Save profile")
                        enabled: App.capabilities.indexOf("profile.v1") >= 0
                                 && profileName.text.trim().length > 0 && profileBio.length <= 300
                        onClicked: App.updateProfile(profileName.text, profileAvatar.text, profileBio.text)
                    }
                }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
                Text {
                    text: qsTr("Signed in as %1 (@%2)").arg(App.selfName).arg(App.selfUsername)
                    color: Theme.text
                    font.pixelSize: Metrics.px(13)
                }
                Text {
                    text: qsTr("Server: %1:%2 · %3").arg(App.accountHost).arg(App.accountPort).arg(App.instanceName)
                    color: Theme.textMuted
                    font.pixelSize: Metrics.px(12)
                }
                Text {
                    text: qsTr("OmaChat %1").arg(App.version)
                    color: Theme.textMuted
                    font.pixelSize: Metrics.px(11)
                }

                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

                Text {
                    text: qsTr("Sign-in methods")
                    color: Theme.text
                    font.pixelSize: Metrics.px(13)
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
                        id: providerRow
                        required property var modelData
                        readonly property var identity: App.oauthIdentities.find(i => i.provider === providerRow.modelData.key)
                        Layout.fillWidth: true
                        spacing: Metrics.px(8)
                        Text {
                            Layout.fillWidth: true
                            color: Theme.text
                            font.pixelSize: Metrics.px(13)
                            text: providerRow.identity ? qsTr("%1 — linked as %2").arg(providerRow.modelData.label).arg(providerRow.identity.username)
                                           : providerRow.modelData.label
                        }
                        FlatButton {
                            enabled: !App.oauthLinkBusy
                            danger: !!providerRow.identity
                            text: providerRow.identity ? qsTr("Unlink")
                                           : (App.oauthLinkBusy ? qsTr("Waiting on browser…") : qsTr("Link"))
                            onClicked: providerRow.identity ? App.unlinkOAuthProvider(providerRow.modelData.key)
                                                 : App.linkOAuthProvider(providerRow.modelData.key)
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: App.oauthLinkMessage.length > 0
                    wrapMode: Text.Wrap
                    color: App.oauthLinkError ? Theme.danger : Theme.textMuted
                    font.pixelSize: Metrics.px(12)
                    text: App.oauthLinkMessage
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.textMuted
                    font.pixelSize: Metrics.px(11)
                    text: qsTr("Linking opens your browser to sign in with that provider, then confirms it here. "
                               + "You can't unlink your last sign-in method without a password set.")
                }

                RowLayout {
                    spacing: Metrics.px(8)
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
                        text: qsTr("Delete this account")
                        onClicked: {
                            removeAccountConfirm.open()
                        }
                    }
                }
                }
            }
        }
    }

    ConfirmDialog {
        id: removeAccountConfirm
        title: qsTr("Permanently delete this account?")
        message: qsTr("%1@%2 will be deleted from the server and this device. Its owned servers and private "
                      + "conversations will also be deleted. This cannot be undone. You must be connected.")
                      .arg(App.accountUser).arg(App.accountHost)
        confirmText: qsTr("Delete account")
        destructive: true
        onConfirmed: {
            dialog.close()
            App.removeAccount(App.accountId)
        }
    }
}
