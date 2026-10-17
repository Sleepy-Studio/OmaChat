# Desktop send recovery across restart

Status: implemented in the working tree, 2026-10-12; integrated validation,
package installation and rollout evidence are recorded in the desktop handoff.
The original October 4 requirements below remain the design baseline.

`SendRecoveryStore` uses libsodium's XChaCha20-Poly1305 secretstream FINAL format
and an independent per-account key in QtKeychain. Insecure fallback is explicitly
disabled. Only encrypted, atomically replaced files enter the private recovery
directory; message content, composer originals and attachment paths never enter
LocalStore. The keyring memory double is available only through an in-process
test factory, not through production configuration.

The daemon's optional `message.send` recovery_id/original fields stage before
network delivery. The production GUI uses this path when a bound secure keyring
is supported. Recovery records bind account, endpoint, actual verified TLS peer
certificate and authenticated user. Nonempty manually pinned certificates must
match; CA-verified connections can bind the actual peer certificate without a
manual pin. Current TLS diagnostics are not evidence of the verified peer.

Limits are 20 records, 128 KiB of serialized plaintext before encryption and
30 days **per account**, not installation-wide. Records in a live daemon remain
pending while their operation runs. After daemon restart, pending becomes
uncertain. Only a correlated server rejection permits explicit retry of the
same immutable record context. Acknowledgement removes recovery; a cleanup
failure reports uncertain delivery and cannot authorize retry. User dismissal
waits for durable deletion. Account logout/removal, explicit duplicate retirement
and automatic OAuth placeholder retirement purge first; purge failure is visible
and prevents successful local retirement. Old queued writes cannot undo purge
or dismissal. User-dismissed ID tombstones remain for the daemon's lifetime;
acknowledged IDs use a temporary ordering guard instead.

The GUI merges recovered outcomes without replacing newer composer text,
reply state or pending files. Account/endpoint/certificate/user scope and
request serials guard late replies. Recovery change events reconcile restored
live pending operations after acknowledgement. Offline outcomes remain visible
within the same identity; uncertain sends never automatically resend.

Filesystem validation rejects symlinks, nonprivate/nonowned files and unexpected
ancestor resolution; reads are bounded. Final file operations run synchronously
in the daemon. This is a single daemon writer, not a cross-process transaction
or an atomic defense against a hostile same-user filesystem writer. Real-keyring
round-trip evidence and injected lock/failure tests are distinct; the user's
actual keyring is not locked for testing.

## Original scoped requirements — 2026-10-04

The storage boundary belongs to the daemon. `LocalStore` is plaintext SQLite
with a WAL and explicitly excludes secrets; do not write message text or local
attachment paths there. Existing `ICredentialStore`/QtKeychain jobs provide a
secret-storage boundary. QtKeychain's installed API defaults its insecure
fallback to false. Keep that behavior and fail visibly if the keyring is locked
or unavailable. `MemoryCredentialStore` remains a process-local test fixture.

Direct bounded JSON records in the keyring could avoid a new cryptographic
implementation. Before choosing that approach, measure the supported backend's
entry-size limits and asynchronous write/delete behavior. If an encrypted file
envelope is required, use an existing vetted format with a separate local key;
do not repurpose the account's network E2E key or design custom cryptography.

Required behavior:

- Bind each record to local account, endpoint, trusted certificate fingerprint
  and authenticated user ID. Switching accounts hides unrelated records.
- Persist a pending record successfully **before** handing a send to the server.
  A storage failure must leave the composer usable with an explicit error.
- On restart, pending records have uncertain delivery and cannot be retried
  automatically. Only explicit server rejection can preserve manual Retry,
  subject to the current connection, permissions and original context guards.
- Successful acknowledgement deletes the record. A crash between acknowledgement
  and deletion may leave an uncertain record; it must never become automatic resend.
- Dismiss deletes durable state with visible failure feedback. Serial ordering
  and account generations must prevent stale asynchronous writes from restoring
  dismissed records.
- Logout, account removal and duplicate-account retirement purge recovery state
  through the existing daemon lifecycle. Specify purge failure behavior explicitly.
- Preserve newer drafts, channel changes, reply/action state and attachment paths;
  missing attachment files must produce a recoverable error.
- Keep the existing cap of 20 unresolved sends. Establish aggregate byte and age
  limits before implementation; 128 KiB/30 days are proposals, not product policy.

Implementation ownership: one coordinator owns `AppController`/`AppActions`
and test wiring; one daemon writer owns the new storage, `DaemonMethods` lifecycle
and credential-store hardening. Relevant current files are
`daemon/src/platform/CredentialStore.*`, `daemon/src/storage/LocalStore.hpp`,
`client/src/controllers/AppController.hpp` (`SendOperation`) and
`client/src/controllers/AppActions.cpp` (`dispatchSend`).

Acceptance needs real-keyring failure/locked/oversize checks plus isolated tests
for restart uncertainty, explicit rejection, acknowledgement/delete crash windows,
async write-versus-dismiss ordering, account/pin changes, purge failure, retention,
and missing files. Current session recovery tests are useful baseline behavior;
they do not verify this design.
