# Security model

## Threats considered

| Threat | Mitigation |
|---|---|
| Network eavesdropping / tampering (control) | TLS 1.3 only. |
| Silent MITM on self-hosted servers | Untrusted certificates are never accepted silently. The GUI/CLI show the SHA-256 fingerprint; the user must trust that exact fingerprint, which is pinned per account. Any other certificate fails again. |
| Voice injection / spoofing | Every UDP datagram is authenticated with the sender's per-session ChaCha20-Poly1305 key; unknown streams are dropped before crypto; sender ids are set by the server, never trusted from clients. |
| Voice replay | 64-packet sliding replay window per stream and packet type, both at the relay and in the client. |
| Password theft from the database | Argon2id (libsodium, interactive limits: 64 MiB, 2 passes); plaintext never stored or logged. Unknown usernames still run the verifier so timing does not reveal them. |
| Token theft | Refresh tokens stored server-side as SHA-256 only and rotated on every use (a replayed token fails). Access tokens are server-memory only and expire after 15 minutes. Client refresh tokens live in the Secret Service keyring (gnome-keyring/KWallet) via QtKeychain — never in config, database, logs, QML or IPC output. |
| Brute force / flooding | Per-IP and per-username login limits, registration limits, per-connection request/message/typing/presence/invite limits, per-IP connection caps, UDP per-stream buckets. |
| Malformed input | Bounded framing (8 MiB control, 1 MiB IPC lines, 1400-byte datagrams); protobuf parse failures close the connection; all names, messages and topics validated server-side; parsers fuzzed in CI. |
| HTML/script injection in messages | Markdown renderer escapes every character first and emits a fixed tag subset; links only for `http`, `https`, `omachat`. Raw HTML is displayed literally. `omachat://invite` links always ask before joining. |
| Other local users | IPC socket 0600 in a 0700 runtime directory plus `SO_PEERCRED` uid check. |
| Privilege escalation | `omachat`, `omachatd` refuse to run as root. The server's systemd unit runs as an unprivileged user with `ProtectSystem=strict` and related hardening. No shell command construction anywhere; the only processes spawned are `omachatd` (fallback launch) and `omachat` (plugin "open"), with fixed argument vectors. |

## Authorization

All permission decisions are made by the server; clients only display the
effective permissions the server sends.

Resolution order for a member in a channel:

1. Server owner → every permission.
2. `base = default role ("Guest") | all assigned roles`.
3. `ADMINISTRATOR` in base → every permission; overrides ignored.
4. For the channel's category, then the channel itself:
   1. apply the default-role override: `base = (base & ~deny) | allow`
   2. combine overrides for the member's roles (allow beats deny at this level) and apply
   3. apply the member's own user override last
5. No `VIEW_CHANNEL` after that → no permissions in the channel.
   Overrides can never grant `ADMINISTRATOR`.

Hierarchy: kicking, banning, server-muting, editing roles and assigning roles
require the actor's highest role to outrank the target (owners always
do). Nobody can grant a permission they do not hold. DMs are visible only to
their participants — not to server owners — and can only be opened with
someone you share a server with.

Built-in roles on server creation: Guest (default, read + join voice), Member
(assigned on joining by invite), Moderator, Admin, Owner.

## Attachments

- Stored under `files.path` (created 0700) as `<attachment id>`, mode 0600.
  Client filenames are metadata only and never become paths: separators turn
  into `_`, control characters are dropped, `.`/`..` are refused.
- Uploads need `VIEW_CHANNEL`, `SEND_MESSAGES` and `ATTACH_FILES`; size is
  capped by `files.max_upload_mb`; chunks must arrive in order and the
  optional SHA-256 is verified before the file is kept. An upload belongs
  to the connection that started it.
- A finished attachment is private to its uploader until a message in the
  same channel claims it (once). Downloads then require `VIEW_CHANNEL` and
  `READ_HISTORY` on that channel; anything else answers "not found".
  Unsent attachments are purged after an hour; deleting a message, channel
  or server deletes its files.
- The daemon saves downloads without overwriting (`name (1).ext`) and caches
  previews under `$XDG_CACHE_HOME/omachat/attachments` (pruned after 30
  days). The GUI hands a received file to the desktop only when its
  *content* sniffs as an image, audio, video, PDF or plain text; anything
  else (scripts, `.desktop` files, archives) is saved to Downloads instead.
  Image previews decode at most 800×600 pixels.
- An interrupted upload can only be resumed (or cancelled) by the user who
  started it; it is discarded after 10 minutes without activity.
- Images pasted into the composer are written to
  `$XDG_CACHE_HOME/omachat/pasted` (0700) so the daemon can upload them,
  and are deleted a week later. Pasting only reads the clipboard on Ctrl+V.

## Group conversations

- Everyone added to a group conversation must share a server with whoever
  adds them, as for direct messages; 3 to 10 people.
- Anyone added sees the conversation's full history. Leaving removes access
  immediately; the last person to leave deletes the conversation and its
  messages.

## Screen sharing

- Sharing needs `STREAM` in the voice channel. The relay forwards video only
  to members of the same voice channel who explicitly asked to watch; a
  permission change that removes `STREAM` ends the share.
- What is shared is chosen in the desktop's own portal dialog; OmaChat never
  captures the screen without it.
- Shared sound is off by default in current source. If you turn it on in
  Settings → Screen sharing or pass `stream start --audio`, it captures
  everything other applications play (not only the shared window; not
  OmaChat's own sound). The GUI asks for confirmation each time sound is
  enabled. A CLI `stream start` without `--audio` stays silent even if an old
  config file contains `audio = true`.
- Decoded frames sit in `$XDG_RUNTIME_DIR/omachat/video` (0700 directory,
  0600 files) and are deleted when you stop watching.

## Several accounts

- Each saved account has its own session, refresh token (keyring entry) and
  pinned certificate. A new account on a host and port whose certificate
  you already trusted inherits that pin rather than asking again.

## Logging

Structured `key=value` logs; journald priorities when run under systemd.
Passwords, tokens, keys and auth headers are never passed to the logger.

## End-to-end encryption (direct and group conversations)

What the server can read:

| | Server can read |
|---|---|
| Direct and group conversations (text, attachments, file names) | **No** — end-to-end encrypted |
| Server channels (text, attachments) | Yes (search, moderation, history for new members) |
| Voice and screen sharing | Encrypted in transit; the relay could decode it |
| Who talks to whom, when, sizes, reactions, conversation names | Yes (metadata) |

How it works (libsodium):

- Each device creates an X25519 key pair per account. The secret key lives
  in your keyring (`e2e/<account>`), never in OmaChat's files; the public
  key is published to the server, at most 10 devices per user.
- A message body (text plus each attachment's name, type, size and file
  key) is encrypted with a fresh random key using XChaCha20-Poly1305. The
  associated data binds it to the conversation, the author and the sending
  device key, so the server cannot move it or re-attribute it.
- That key is `crypto_box`ed from the sending device to every device of
  every participant (including your own other devices). The box also carries
  a hash of the ciphertext, so a participant who learned the key cannot
  swap in a different body under someone else's name.
- Attachments are encrypted before upload with `crypto_secretstream`
  (64 KiB chunks, truncation detected) under a random per-file key; the
  server stores them as `<random>.enc`.
- Keys are trusted on first use and pinned per contact. When a contact's
  devices change afterwards, you are warned, their safety number changes,
  and any earlier verification is cleared. Compare the 60-digit safety
  number with them outside OmaChat and mark it verified (lock icon in the
  conversation, or `omachatctl e2e safety USER` / `e2e verify USER`).
- No silent downgrade: when the server supports it, conversations are always
  encrypted. If a participant has no device key (an old client), sending
  fails with an explanation instead of falling back to plaintext.

Limits, stated plainly:

- **No forward secrecy.** Device keys are long-lived; someone who steals a
  device key can read every message that was ever sent to that device.
  The migration plan is in `docs/security-roadmap.md`.
- A device added later cannot read messages sent before it existed.
  Removing an account from a device revokes its key.
- Trust on first use: the very first key the server hands you for a
  contact is believed until you compare safety numbers.
- Messages from before encryption existed stay as they were (plaintext).
- Search cannot find encrypted messages (the server cannot index them).

## Not in scope for 0.2

- End-to-end encryption of server channels, voice and screen sharing (see
  above): the server can read server channels and, because it re-seals media
  per recipient, could decode voice and video. Self-host a server you trust.
- Account recovery, 2FA, OIDC/passkeys.

Forward secrecy and end-to-end media are future protocol work, not properties
of the current build. The threat model and acceptance gates are in
`docs/security-roadmap.md`.
