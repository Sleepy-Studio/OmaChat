# Desktop send recovery across restart

Status: scoped design, 2026-10-04. **Not implemented.** Current failed sends remain
in GUI memory; active daemon transfers are rediscovered. This design records the
next bounded persistence slice from the desktop continuation.

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
