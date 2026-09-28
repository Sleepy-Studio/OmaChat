# Protocols

OmaChat has three wire formats: the reliable server protocol, the UDP media
protocol ([media.md](media.md)), and the local IPC protocol.

## Reliable protocol (client ⇄ server)

- Transport: TCP + TLS 1.3 (TLS 1.2 and below refused). Default port 6473.
- Framing: `u32` big-endian length, then a serialized `omachat.proto.Envelope`
  (`protocol/network.proto`). Frames above 8 MiB close the connection.
- Every `Envelope` carries `request_id` (echoed in the reply; 0 for pushed
  events), an optional `Error`, and one payload.
- TCP keepalive (30 s idle, 3 × 10 s probes) detects dead peers without
  application pings.

### Handshake and versioning

1. Client sends `Hello{protocol_major, protocol_minor, client_version, capabilities}`.
2. Server replies `HelloReply{…, instance_name, registration_open, media_udp_port}`
   or `ERROR_PROTOCOL_MISMATCH` (different major) and closes.
   Current version: **1.1**. Minor versions negotiate through capability
   strings (`resume`, `voice.opus`, `media.chacha20poly1305`, `search.fts`,
   `attachments`). `HelloReply.max_upload_bytes` is 0 when a server takes no
   attachments.
3. Unauthenticated connections have 30 s to finish authenticating.

### Authentication and sessions

| Request | Result |
|---|---|
| `Register{username,password,display_name}` | `AuthResult` |
| `Login{username,password}` | `AuthResult` |
| `Refresh{refresh_token}` | `AuthResult` with a **rotated** refresh token (old one dies) |
| `Resume{access_token, session_id, last_sequence}` | `ResumeResult{replayed_events}` then the missed events; `ERROR_RESUME_FAILED` = authenticated but you must `Sync`; `ERROR_AUTHENTICATION` = use your refresh token |
| `Logout` | deletes the server session |

`AuthResult` = user, access token (in-memory on the server, 15 min),
refresh token (stored as SHA-256 on the server, 30 days), session id.

### Events

After `Sync` returns a `SyncState` (servers, channels with *your* effective
permissions, roles, members, users, voice states, `last_sequence`), the
server pushes `Event`s: message create/update/delete, channel
create/update/delete, member join/update/leave, presence, voice state,
reactions, role update/delete, typing (ephemeral, sequence 0, not
replayed), server create/update/delete, user update and
`permissions_changed` (clients resync). Clients never poll.

### Ids

64-bit snowflakes: bit 63 = 0; 41 bits of milliseconds since
2025-01-01T00:00:00Z; 10-bit node id (`server.node_id`); 12-bit sequence.
Ids sort by creation time. In JSON (IPC) ids are **decimal strings**,
because JavaScript numbers cannot hold 64-bit integers.

### Message history

`GetMessages{channel_id, before_message_id, limit}` → newest-first page,
default 50, max 100, with `has_more`. `SearchMessages` uses SQLite FTS5;
user input is quoted so it can never use FTS query syntax.

### Attachments

Files ride the control connection in chunks, so no extra port is needed.

1. `BeginUpload{channel_id, filename, mime_type, size}` → `UploadTicket{attachment_id, chunk_size}`
   (512 KiB). `ERROR_TOO_LARGE` above `files.max_upload_mb`.
2. `UploadChunk{attachment_id, offset, data}` → `Ok`. `offset` must equal the
   bytes received so far; a gap or overrun cancels the upload. Clients may
   pipeline chunks (omachatd keeps four in flight).
3. `FinishUpload{attachment_id, sha256?}` → `Attachment`. The attachment is
   now pending: visible only to its uploader, purged after an hour.
4. `SendMessage{…, attachment_ids}` (≤ 10) claims pending attachments of the
   same author and channel. The text may then be empty.

`CancelUpload` drops an upload in progress or a pending attachment. An
upload in progress dies with its connection. `Download{attachment_id,
offset, length}` → `FileChunk{offset, data, total_size}` for anyone who may
read the message's channel history.

### Rate limits (per connection unless noted)

| Bucket | Burst / refill |
|---|---|
| any request | 120 / 40 per s |
| messages, edits, reactions | 10 / 2 per s |
| typing | 4 / 0.5 per s (excess silently dropped) |
| presence | 5 / 0.2 per s |
| invites create/join | 5 / 0.1 per s |
| history & search | 30 / 5 per s |
| upload starts | 10 / 1 per s |
| upload and download chunks | 64 / 40 per s |
| login (per IP) · (per username) | 10 per min · 5 per min |
| registration (per IP) | 5 per 10 min |
| connections per IP | `limits.max_connections_per_ip` (16) |

Rejections carry `ERROR_RATE_LIMITED` with `retry_after_ms`.

## Local IPC (omachatd ⇄ GUI, CLI, plugin)

Unix socket `$XDG_RUNTIME_DIR/omachat/omachat.sock` (override:
`OMACHAT_SOCKET`), directory 0700, socket 0600, and every connection's
`SO_PEERCRED` uid must equal the daemon's. Newline-delimited compact JSON,
lines ≤ 1 MiB. A client that stops reading is disconnected once 8 MiB is
queued for it.

```json
→ {"v":1,"id":7,"method":"voice.join","params":{"channel":"General"}}
← {"v":1,"id":7,"ok":true,"result":{…}}
← {"v":1,"id":7,"ok":false,"error":{"code":"PermissionDenied","message":"…"}}
← {"v":1,"event":"voice.state","data":{…}}
```

JSON rather than protobuf so the Quickshell plugin and shell scripts can use
it without code generation.

Error codes: `BadRequest UnknownMethod NotConnected NetworkError
AuthenticationError PermissionDenied NotFound Conflict RateLimited
ProtocolMismatch CertificateError ServerUnavailable MediaDeviceUnavailable
StorageError Timeout TooLarge Internal`.

### Methods

Channel/server/user parameters accept an id, a name, or `Server/channel`.

| Area | Methods |
|---|---|
| daemon | `daemon.status`, `daemon.version`, `state.snapshot`, `events.subscribe {topics?}`, `events.unsubscribe` |
| accounts | `account.list`, `account.add`, `account.login`, `account.register`, `account.logout`, `account.remove`, `connect`, `disconnect`, `certificate.trust {fingerprint}` |
| servers | `server.list`, `server.create`, `server.join {invite}`, `server.leave`, `server.delete`, `invite.create`, `invite.list`, `member.list` |
| channels | `channel.list`, `channel.join`, `channel.create`, `channel.update`, `channel.delete`, `channel.mute`, `dm.open` |
| messages | `message.history`, `message.send {files?}`, `message.edit`, `message.delete`, `message.search`, `message.react`, `typing`, `presence.set` |
| attachments | `attachment.download {attachment, filename?, to?: downloads\|cache\|/abs/path, size?}` → `{path, cached}`, `transfer.list`, `transfer.cancel {id}` |
| voice | `voice.join`, `voice.leave`, `voice.mute`, `voice.unmute`, `voice.toggle_mute`, `voice.deafen`, `voice.undeafen`, `voice.toggle_deafen`, `voice.mode`, `voice.stats`, `ptt.begin`, `ptt.end` |
| audio | `audio.devices`, `audio.settings`, `audio.set`, `audio.user_volume` |
| moderation | `moderation.kick`, `moderation.ban`, `moderation.unban`, `moderation.voice_mute`, `role.create`, `role.update`, `role.delete`, `role.assign`, `override.set`, `override.list` |
| ui/config | `ui.focus`, `ui.navigate`, `config.get`, `config.set_notifications`, `config.reload` |

Push-to-talk held by a client is released automatically if that client
disconnects.

### Events

`status` (full status object, coalesced), `connection`, `state.reset`
(refetch `state.snapshot`), `message.created/updated/deleted`, `reaction`,
`typing`, `channel.created/updated/deleted/muted`, `member.joined/updated/left`,
`server.updated/removed`, `user.updated`, `presence`, `role.updated/deleted`,
`voice.state`, `voice.speaking`, `voice.self`, `voice.ptt`, `voice.error`,
`audio.devices`, `ui.navigate`, `transfer.progress` (`{id, direction,
name, transferred, total}` plus `complete` or `error` at the end).

`message.send` with `files` (absolute paths, read by the daemon) and
`attachment.download` answer when the transfer ends; clients should call
them without a timeout. Transfers belong to the daemon, so closing the GUI
does not cancel them.

### Status JSON (stable contract: `omachatctl status --json`)

```json
{
  "version": "0.1.0",
  "connected": true,
  "state": "connected",
  "error": null,
  "reconnect_in_ms": 0,
  "account": {"id": "1", "host": "chat.example.org", "port": 6473, "username": "howie"},
  "instance": "OmaChat",
  "max_upload_bytes": 52428800,
  "user": {"id": "2301…", "username": "howie", "display_name": "Howie", "status": "online"},
  "server": {"id": "2301…", "name": "Sleepy Studio"},
  "voice": {"joined": true, "pending": false, "channel_id": "2301…", "channel": "Development",
            "muted": false, "deafened": false, "mode": "vad", "ptt": false, "transmitting": false,
            "registered": true, "count": 2,
            "participants": [{"user_id": "…", "name": "Alice", "speaking": true, "muted": false, "deafened": false}]},
  "audio": {"backend": "pipewire", "input": "default", "output": "default", "error": ""},
  "clients": 2
}
```

`state` is one of `not_configured, connecting, authenticating,
login_required, synchronizing, connected, reconnecting, offline,
disconnected, error`. Keys are only ever added in minor releases.
