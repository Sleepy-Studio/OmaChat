import QtQuick
import Quickshell
import Quickshell.Io

// Persistent client for omachatd's local socket
// ($XDG_RUNTIME_DIR/omachat/omachat.sock, newline-delimited JSON).
//
// Live state arrives as pushed "status" events; nothing here polls and no
// processes are spawned. While omachatd is absent the plugin shows a
// disconnected state and retries with a slow backoff. This file holds no
// credentials and speaks no network protocol.
Item {
  id: root

  visible: false
  width: 0
  height: 0

  readonly property string socketPath: {
    var override = Quickshell.env("OMACHAT_SOCKET")
    if (override) return String(override)
    var runtime = Quickshell.env("XDG_RUNTIME_DIR")
    return String(runtime || "/tmp") + "/omachat/omachat.sock"
  }

  readonly property var activeSocket: socketLoader.item
  readonly property bool connected: !!(activeSocket && activeSocket.connected)

  // Mirror of `omachatctl status --json`.
  property var status: ({})
  readonly property string connectionState: connected ? String(status.state || "starting") : "daemon-offline"
  readonly property var voice: status.voice || ({})
  readonly property bool inVoice: voice.joined === true
  readonly property bool muted: voice.muted === true
  readonly property bool deafened: voice.deafened === true
  readonly property string channelName: String(voice.channel || "")
  readonly property var participants: voice.participants || []
  readonly property int speakerCount: participants.length
  readonly property string serverName: status.server ? String(status.server.name || "") : ""
  readonly property bool reconnecting: connectionState === "reconnecting" || connectionState === "connecting" || connectionState === "synchronizing"

  property int nextId: 1
  property int attempt: 0

  function send(method, params) {
    var s = activeSocket
    if (!s || !s.connected) return false
    s.write(JSON.stringify({ v: 1, id: nextId++, method: method, params: params || {} }) + "\n")
    s.flush()
    return true
  }

  function toggleMute() { send("voice.toggle_mute") }
  function toggleDeafen() { send("voice.toggle_deafen") }
  function leaveVoice() { send("voice.leave") }
  function openApp() { Quickshell.execDetached(["omachat"]) }

  function handleLine(line) {
    var msg
    try { msg = JSON.parse(line) } catch (e) { return }
    if (!msg || typeof msg !== "object") return
    if (msg.event === "status" || msg.event === "connection") {
      status = msg.data || ({})
      return
    }
    // Replies to daemon.status and voice.* carry the status or voice object.
    if (msg.ok === true && msg.result) {
      if (msg.result.state !== undefined) status = msg.result
      else if (msg.result.joined !== undefined) {
        var s = Object.assign({}, status)
        s.voice = msg.result
        status = s
      }
    }
  }

  onConnectedChanged: {
    if (connected) {
      attempt = 0
      send("events.subscribe", { topics: ["status", "connection"] })
      send("daemon.status")
    } else {
      status = ({})
    }
  }

  Component {
    id: socketComponent
    Socket {
      path: root.socketPath
      connected: true
      parser: SplitParser {
        splitMarker: "\n"
        onRead: function(line) { root.handleLine(line) }
      }
    }
  }

  Loader {
    id: socketLoader
    active: true
    sourceComponent: socketComponent
  }

  // A failed connect leaves a dead socket; each retry creates a fresh one.
  // Only runs while omachatd is unreachable, backing off to 15 s.
  Timer {
    interval: Math.min(15000, 500 * Math.pow(2, Math.min(root.attempt, 5)))
    repeat: true
    running: !root.connected
    onTriggered: {
      root.attempt += 1
      socketLoader.active = false
      socketLoader.active = true
    }
  }
}
