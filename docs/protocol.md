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
   Current version: **1.2**. Minor versions negotiate through capability
   strings (`resume`, `voice.opus`, `media.chacha20poly1305`, `search.fts`,
   `attachments`; since 1.2 `search.server`, `dm.group`,
   `attachments.resume`, `video.h264`, `e2e.v1`). `HelloReply.max_upload_bytes` is 0
   when a server takes no attachments. Clients check a capability before
   using the feature, so a 1.2 client works with a 1.1 server.
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
user input is quoted so it can never use FTS query syntax. With
`channel_id = 0` and `server_id` set it searches every channel of that
server the caller may read (`VIEW_CHANNEL` + `READ_HISTORY`); results are
newest first across channels, at most 50.

### Group conversations (`dm.group`)

`CreateGroupDm{user_ids, name?}` → `Channel` (type `GROUP_DM`, 3–10 people
including the caller, each sharing a server with the caller).
`AddGroupDmRecipient{channel_id, user_id}` → `Channel`: any participant
may add someone they share a server with; existing members get
`channel_update`, the new one `channel_create` (and the full history).
`UpdateChannel{name}` renames it (any participant; no topics).
`LeaveGroupDm{channel_id}` → `Ok`: the leaver gets `channel_delete`, the
others `channel_update`; the last one out deletes the conversation.

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

`CancelUpload` drops an upload in progress or a pending attachment (from
any of the uploader's connections). When a connection closes mid-upload
the upload waits, for at most 10 minutes of inactivity, for
`ResumeUpload{attachment_id}` from the same user on another connection
(`attachments.resume`): the reply is an `UploadTicket` whose `received`
says where to continue. `Download{attachment_id, offset, length}` →
`FileChunk{offset, data, total_size}` for anyone who may read the
message's channel history.

### End-to-end encryption (`e2e.v1`)

`PublishDeviceKey{public_key}` (32-byte X25519, ≤ 10 per user),
`RevokeDeviceKey{public_key}`, `GetDeviceKeys{user_ids ≤ 50}` →
`DeviceKeyList` (only yourself and people you share a server or
conversation with). A change is pushed as `device_keys_changed{user_id}`
to everyone who can see that user. In direct and group conversations
`SendMessage`/`EditMessage` carry `encrypted` (an `E2EPayload`, ≤ 64 KiB)
and an empty `content`; the server stores and relays it untouched and
refuses it in server channels. `E2EPayload`, `E2EBody` and `E2EFile` are
defined in `network.proto`; the scheme is in [security.md](security.md).

### Screen sharing (`video.h264`)

In a voice channel, `SetStreaming{streaming}` (needs `STREAM`) marks you as
sharing (`VoiceState.streaming`) and lets the relay forward your video
packets. `WatchStream{user_id, watch}` subscribes you to a sharer in the
same voice channel; the relay then asks the sharer for a keyframe. Video
never reaches anyone who did not ask to watch. Packet format:
[media.md](media.md#screen-sharing).

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
| accounts | `account.list`, `account.add`, `account.login`, `account.register`, `account.logout`, `account.remove`, `account.switch {account}`, `connect`, `disconnect`, `certificate.trust {fingerprint}` |
| servers | `server.list`, `server.create`, `server.join {invite}`, `server.leave`, `server.delete`, `invite.create`, `invite.list`, `member.list` |
| channels | `channel.list`, `channel.join`, `channel.create`, `channel.update`, `channel.delete`, `channel.mute`, `dm.open`, `dm.send {user, content}`, `dm.create {users, name?}`, `dm.add {channel, user}`, `dm.leave {channel}` |
| messages | `message.history`, `message.send {files?}`, `message.edit`, `message.delete`, `message.search {channel \| server, query}`, `message.react`, `typing`, `presence.set` |
| attachments | `attachment.download {attachment, filename?, to?: downloads\|cache\|/abs/path, size?}` → `{path, cached}`, `transfer.list`, `transfer.cancel {id}` |
| voice | `voice.join`, `voice.leave`, `voice.mute`, `voice.unmute`, `voice.toggle_mute`, `voice.deafen`, `voice.undeafen`, `voice.toggle_deafen`, `voice.mode`, `voice.stats`, `ptt.begin`, `ptt.end` |
| encryption | `e2e.status` → `{enabled, ready, device}`, `e2e.safety {user}` → `{number, devices, verified}`, `e2e.verify {user, verified?}` |
| screen sharing | `stream.start {audio?}` (answers after the desktop picker; no timeout), `stream.stop`, `stream.watch {user}` → `{path}`, `stream.unwatch {user}`, `stream.stats`, `video.settings`, `video.set` |
| audio | `audio.devices`, `audio.settings`, `audio.set`, `audio.user_volume` |
| moderation | `moderation.kick`, `moderation.ban`, `moderation.unban`, `moderation.voice_mute`, `role.create`, `role.update` (fields left out are kept), `role.delete`, `role.assign`, `override.set {channel, role \| user, allow, deny, remove?}`, `override.list` |
| ui/config | `ui.focus`, `ui.navigate`, `config.get`, `config.set_notifications`, `config.reload` |

Push-to-talk held by a client is released automatically if that client
disconnects.

Messages of encrypted conversations arrive decrypted, with `"e2e": "ok"`
(`"unverified"`: the sending device is not one we know for the author;
`"undecryptable"`: not addressed to this device). Their attachments show
the real file name, type and size, and `attachment.download` decrypts them.

### Events

`status` (full status object, coalesced), `connection`, `state.reset`
(refetch `state.snapshot`), `message.created/updated/deleted`, `reaction`,
`typing`, `channel.created/updated/deleted/muted`, `member.joined/updated/left`,
`server.updated/removed`, `user.updated`, `presence`, `role.updated/deleted`,
`voice.state`, `voice.speaking`, `voice.self`, `voice.ptt`, `voice.error`,
`audio.devices`, `ui.navigate`, `transfer.progress` (`{id, direction,
name, transferred, total, waiting, account}` plus `complete` or `error` at
the end; `waiting` while the connection is down), `account.activity`
(`{account, unread, mentions}` for a background account), `e2e.keys_changed`
(`{user_id, name, self}`: a contact's devices changed), `stream.ended`
(`{user_id?}`: a watched share, or your own, stopped).

With several accounts, everything above describes the **active** account;
`account.switch` changes it and is followed by `state.reset`.

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
  "accounts": [{"id": "1", "host": "chat.example.org", "port": 6473, "username": "howie",
                "state": "connected", "active": true, "instance": "OmaChat", "unread": 0, "mentions": 0}],
  "instance": "OmaChat",
  "max_upload_bytes": 52428800,
  "capabilities": ["resume", "search.server", "dm.group", "attachments", "video.h264", "…"],
  "user": {"id": "2301…", "username": "howie", "display_name": "Howie", "status": "online"},
  "server": {"id": "2301…", "name": "Sleepy Studio"},
  "voice": {"joined": true, "pending": false, "channel_id": "2301…", "channel": "Development",
            "muted": false, "deafened": false, "mode": "vad", "ptt": false, "transmitting": false,
            "registered": true, "count": 2, "streaming": false,
            "watching": [{"user_id": "…", "path": "/run/user/1000/omachat/video/….frame"}],
            "participants": [{"user_id": "…", "name": "Alice", "speaking": true, "muted": false,
                              "deafened": false, "streaming": true}]},
  "audio": {"backend": "pipewire", "input": "default", "output": "default", "error": ""},
  "clients": 2
}
```

`state` is one of `not_configured, connecting, authenticating,
login_required, synchronizing, connected, reconnecting, offline,
disconnected, error`. Keys are only ever added in minor releases.
