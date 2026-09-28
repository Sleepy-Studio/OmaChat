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

## Logging

Structured `key=value` logs; journald priorities when run under systemd.
Passwords, tokens, keys and auth headers are never passed to the logger.

## Not in scope for 0.1

- End-to-end encryption: the server can read messages and, because it
  re-seals media per recipient, could decode voice. Self-host a server you trust.
- Attachments (planned for 0.2; `ObjectStore` path config exists but the
  upload endpoint does not).
- Account recovery, 2FA, OIDC/passkeys.
