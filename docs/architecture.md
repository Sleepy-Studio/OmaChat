# Architecture

OmaChat is four native programs plus an optional shell plugin.

```text
                  omachat-server  (self-hosted)
                   │ TCP+TLS 1.3 : control, text, state   (default 6473)
                   │ UDP         : encrypted voice         (default 6474)
                   ▼
                omachatd  (systemd --user service)
   owns: server session, auth tokens (keyring), reconnect, presence,
         voice engine (PipeWire + Opus), notifications, local cache DB
                   │ JSON lines over $XDG_RUNTIME_DIR/omachat/omachat.sock (0600)
        ┌──────────┼──────────────┐
        ▼          ▼              ▼
   omachat      omachatctl    Omarchy bar widget (optional)
   Qt Quick GUI  CLI / scripts  Quickshell plugin
```

Dependency direction is strictly downward: the plugin and GUI depend on the
daemon's public IPC contract; nothing in the daemon or server knows the
plugin exists. Closing the GUI leaves the daemon — and your voice call —
running.

## Repository layout

| Path | Contents |
|---|---|
| `protocol/network.proto` | Reliable wire protocol (protobuf, lite runtime) |
| `shared/` | Logging, XDG paths, snowflake ids, permission resolver, validation, framing, media packet + AEAD, IPC client, client config |
| `server/` | `omachat-server`: storage (SQLite), in-memory guild state, auth, request handlers, UDP media relay |
| `daemon/` | `omachatd`: server connection, client model, IPC server, voice engine, PipeWire backend, notifications, keyring |
| `cli/` | `omachatctl` |
| `client/` | `omachat` GUI: C++ controller + models, QML views |
| `integrations/omarchy/` | Omarchy bar widget plugin and install/uninstall scripts |
| `packaging/` | desktop entry, icon, systemd units, server config example, PKGBUILD |
| `tests/` | unit, integration (real TLS/UDP, in-process server and daemons), fuzz |

## Server

Single process, Qt event loop. `Store` holds all SQL (portable SQL via Qt SQL
so a PostgreSQL driver can be added; FTS5 and PRAGMAs are the only
SQLite-specific parts). `State` mirrors guild structure in memory and
resolves permissions (cached, invalidated on any role/override/member
change). Handlers are split by domain (`AuthHandlers.cpp`,
`ServerHandlers.cpp`, `MessageHandlers.cpp`, `VoiceHandlers.cpp`,
`ModerationHandlers.cpp`). Argon2id hashing runs on the thread pool so a
login never stalls other users.

Events are appended to a bounded `EventLog` (20 000 entries) with an explicit
recipient list; that is what makes session resume possible.

## Daemon

`ServerConnection` is the connection state machine:

```text
not_configured → connecting → authenticating ─┬→ login_required (waits for credentials)
                                              └→ synchronizing → connected
connected ──(socket lost)──→ reconnecting ──(backoff, jitter)──→ connecting …
          ──(network down)──→ offline ──(network up)──→ connecting
any ──(untrusted certificate / protocol mismatch)──→ error (no auto-retry)
```

Reconnect first tries `Resume` (access token + last event sequence; the
server replays missed events). If that is impossible it uses the refresh
token from the keyring, then performs a full sync; if you were in voice it
rejoins automatically.

`ClientState` is the daemon's model of what the server told it; it emits
named IPC events (`message.created`, `voice.state`, …). The GUI and plugin
never talk to the server.

## End-to-end encryption

`crypto/E2E.*` is the pure scheme (libsodium: seal/open, file streams,
safety numbers); `crypto/E2EManager` holds one account's device key
(keyring), its contacts' key directory and local pins, and is wired into
`ClientState::messageJson` as the decryptor. The send path
(`Daemon::sendMessage`) encrypts text and files for conversations.

## Screen sharing

`video/ScreenSource` (portal + PipeWire, or a synthetic pattern),
`video/H264Codec` (libavcodec), and `video/VideoManager` (sending pipeline,
per-sharer viewers, shared-memory frame files for the GUI's
`VideoFrameItem`). See [media.md](media.md#screen-sharing).

## Voice engine

See [media.md](media.md). Realtime PipeWire callbacks only touch lock-free
SPSC rings and atomics; an encoder thread and a mixer thread are woken with
atomic wait/notify. With no voice session, audio streams are closed and no
thread wakes up.

## GUI

`AppController` (QML singleton `App`) mirrors daemon status into properties
and `RowListModel`/`MessageListModel`s, and maps user intent to IPC calls.
QML contains layout and presentation only. `ThemeProvider` (singleton
`Theme`) is the only place colors come from:

```text
$XDG_STATE_HOME/omarchy/current/theme/colors.toml → ThemeProvider → Theme.* in QML
```

It falls back to a built-in dark palette outside Omarchy, enforces readable
contrast, and reloads live on theme switches via file-system notifications.

Messages use a `ListView` with `BottomToTop` layout over a model that is
newest-first; older pages load through Qt's `fetchMore`, 50 at a time, so
only visible delegates exist.

## Threads

| Process | Threads |
|---|---|
| server | event loop; Qt thread pool for Argon2id |
| omachatd | event loop (network, IPC, UDP receive); PipeWire loop thread (RT callbacks); encoder; mixer |
| GUI | Qt GUI thread + render thread; all I/O is asynchronous IPC |
