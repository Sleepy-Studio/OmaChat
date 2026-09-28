import QtQuick
import QtQuick.Layouts
import Quickshell
import qs.Commons
import qs.Ui

// OmaChat bar widget + compact voice panel.
//
// Thin UI over omachatd's local socket: no credentials, no network
// protocol, no audio. If omachatd is not running the widget shows a
// disconnected state and the rest of the shell is unaffected.
Panel {
  id: root
  moduleName: "io.github.howieduhzit.omachat"
  manageIpc: false

  readonly property color foreground: bar ? bar.foreground : Color.foreground
  readonly property color urgent: bar ? bar.urgent : Color.urgent
  readonly property color dim: Qt.darker(foreground, 1.6)
  readonly property color good: Color.accent
  readonly property string fontFamily: bar ? bar.fontFamily : Style.font.family

  // Nerd Font glyphs (Omarchy ships a Nerd Font).
  readonly property string glyphHeadset: String.fromCodePoint(0xF02CE)
  readonly property string glyphMicOff: String.fromCodePoint(0xF036D)
  readonly property string glyphHeadphonesOff: String.fromCodePoint(0xF02D0)
  readonly property string glyphMic: String.fromCodePoint(0xF036C)
  readonly property string glyphHeadphones: String.fromCodePoint(0xF02CB)

  readonly property bool hideWhenIdle: setting("hideWhenIdle", false)

  readonly property string barText: {
    if (!client.connected) return glyphHeadset
    var icon = client.deafened ? glyphHeadphonesOff : (client.muted ? glyphMicOff : glyphHeadset)
    if (!client.inVoice || (bar && bar.vertical)) return icon
    var name = client.channelName.length > 14 ? client.channelName.substring(0, 13) + "…" : client.channelName
    return icon + " " + name + " · " + client.speakerCount
  }

  readonly property string stateText: {
    if (!client.connected) return "OmaChat is not running"
    switch (client.connectionState) {
    case "connected": return client.inVoice ? "In voice" : "Connected"
    case "reconnecting": return "Reconnecting…"
    case "connecting": return "Connecting…"
    case "synchronizing": return "Synchronizing…"
    case "offline": return "Offline"
    case "login_required": return "Log in required"
    case "not_configured": return "Not set up"
    case "error": return "Connection error"
    default: return "Disconnected"
    }
  }

  implicitWidth: button.visible ? button.implicitWidth : 0
  implicitHeight: button.implicitHeight

  OmaChatClient { id: client }

  WidgetButton {
    id: button
    anchors.fill: parent
    visible: !(root.hideWhenIdle && !client.inVoice)
    bar: root.bar
    text: root.barText
    dimmed: !client.connected || client.connectionState !== "connected"
    active: client.muted || client.deafened
    tooltipText: "OmaChat · " + root.stateText
                 + (client.inVoice ? " · " + client.channelName : "")
                 + " · Left: controls · Middle: mute · Right: open OmaChat"
    onPressed: function(buttonCode) {
      if (buttonCode === Qt.MiddleButton) client.toggleMute()
      else if (buttonCode === Qt.RightButton) client.openApp()
      else root.toggle()
    }
  }

  KeyboardPanel {
    id: panel
    anchorItem: button
    owner: root
    bar: root.bar
    open: root.opened
    focusTarget: keys
    contentWidth: panel.fittedContentWidth(Style.space(300))
    contentHeight: panel.fittedContentHeight(content.implicitHeight, Style.space(460))

    PanelKeyCatcher {
      id: keys
      anchors.fill: parent
      onCloseRequested: root.close()
      onTabRequested: function(direction) { root.switchPanel(direction) }
      onTextKey: function(t) {
        if (t === "m" || t === "M") client.toggleMute()
        else if (t === "d" || t === "D") client.toggleDeafen()
        else if (t === "l" || t === "L") client.leaveVoice()
        else if (t === "o" || t === "O") { client.openApp(); root.close() }
      }

      ColumnLayout {
        id: content
        width: parent.width
        spacing: Style.space(10)

        // Header
        ColumnLayout {
          spacing: 2
          Text {
            text: "OmaChat"
            color: root.foreground
            font.family: root.fontFamily
            font.pixelSize: Style.font.subtitle
            font.bold: true
          }
          Text {
            text: client.serverName.length > 0 ? client.serverName + " · " + root.stateText : root.stateText
            color: root.dim
            font.family: root.fontFamily
            font.pixelSize: Style.font.caption
          }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Qt.alpha(root.foreground, 0.15) }

        // Voice channel + speakers
        Text {
          visible: client.inVoice
          text: root.glyphHeadset + "  " + client.channelName
          color: root.foreground
          font.family: root.fontFamily
          font.pixelSize: Style.font.body
          font.bold: true
        }
        Text {
          visible: !client.inVoice
          Layout.fillWidth: true
          wrapMode: Text.Wrap
          text: client.connected ? "Not in a voice channel." : "Start OmaChat to connect."
          color: root.dim
          font.family: root.fontFamily
          font.pixelSize: Style.font.body
        }

        Repeater {
          model: client.inVoice ? client.participants : []
          delegate: RowLayout {
            required property var modelData
            Layout.fillWidth: true
            spacing: Style.space(8)
            Rectangle {
              implicitWidth: Style.space(8)
              implicitHeight: implicitWidth
              radius: implicitWidth / 2
              color: modelData.speaking ? root.good : Qt.alpha(root.foreground, 0.25)
            }
            Text {
              Layout.fillWidth: true
              text: modelData.name
              elide: Text.ElideRight
              color: modelData.speaking ? root.foreground : root.dim
              font.family: root.fontFamily
              font.pixelSize: Style.font.body
            }
            Text {
              visible: modelData.muted || modelData.deafened
              text: modelData.deafened ? root.glyphHeadphonesOff : root.glyphMicOff
              color: root.urgent
              font.family: root.fontFamily
              font.pixelSize: Style.font.body
            }
          }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Qt.alpha(root.foreground, 0.15) }

        RowLayout {
          spacing: Style.space(6)
          PanelActionButton {
            iconText: client.muted ? root.glyphMicOff : root.glyphMic
            tooltipText: (client.muted ? "Unmute" : "Mute") + " (m)"
            foreground: client.muted ? root.urgent : root.foreground
            fontFamily: root.fontFamily
            enabled: client.connected
            onClicked: client.toggleMute()
          }
          PanelActionButton {
            iconText: client.deafened ? root.glyphHeadphonesOff : root.glyphHeadphones
            tooltipText: (client.deafened ? "Undeafen" : "Deafen") + " (d)"
            foreground: client.deafened ? root.urgent : root.foreground
            fontFamily: root.fontFamily
            enabled: client.connected
            onClicked: client.toggleDeafen()
          }
          PanelActionButton {
            visible: client.inVoice
            iconText: String.fromCodePoint(0xF03D7) // phone-hangup
            tooltipText: "Leave voice (l)"
            foreground: root.urgent
            fontFamily: root.fontFamily
            onClicked: client.leaveVoice()
          }
          Item { Layout.fillWidth: true }
          PanelActionButton {
            iconText: String.fromCodePoint(0xF0379) // open-in-new
            tooltipText: "Open OmaChat (o)"
            foreground: root.foreground
            fontFamily: root.fontFamily
            onClicked: { client.openApp(); root.close() }
          }
        }
      }
    }
  }
}
