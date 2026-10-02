# Android portable crypto source review

Reviewed 2026-10-01 during implementation. This is an engineering source review
and interoperability validation, **not an independent cryptographic audit**.
No forward secrecy, key transparency or complete mobile-release claim is made.

## Extraction and wire compatibility

The source of truth was desktop `daemon/src/crypto/E2E.cpp`, its manager,
`tests/unit/test_e2e.cpp`, parser fuzz targets, device-directory handlers and the
shared schema. `MessageCrypto.*` retains XChaCha20-Poly1305 with associated data
`omachat-e2e-v1 | channel uint64 BE | author uint64 BE | sender public key`.
Every device receives a `crypto_box_easy` wrap of the 32-byte content key plus
BLAKE2b-256 of `nonce | ciphertext`. The hash prevents a participant who learned
the content key from substituting a different body under another sender's wraps.
Duplicate recipients are ignored; the sender's current device is included.
Kotlin serializes the existing E2EBody/E2EPayload protobufs, without inventing a
cryptographic format. Source comments now include the sender-key AD and hash.

Safety numbers retain sorted keys, per-user BE64 identity, the existing domain,
4096 BLAKE2b rounds and twelve five-digit groups. A fixed vector independently
computed with Python's hashlib guards the serialization, rather than checking
only that both extracted implementations agree.

`FileCrypto.*` retains a 24-byte secretstream header, 64 KiB plaintext chunks,
17-byte per-chunk overhead and exactly one final tag. Empty and exact-multiple
files are covered. Truncation, altered ciphertext and trailing data fail.
Desktop adapters stage output and retain QSaveFile atomic destination replacement;
Android authenticates staging fully before exporting to a document provider.
The portable file cap is 4 GiB, matching the maximum configurable server limit;
Android imposes 100 MiB. Stream buffers remain fixed-sized.

## Reviewed boundaries and trust

- JNI caps body bytes, array count, element sizes and aggregate input before
  copying; the core independently caps bodies and device wraps. Protobuf version,
  sender/nonce/box lengths, requested context and body authentication are checked.
- RAII wipes native identity copies, content keys, wraps' secret bytes and
  secretstream state on exit, including failures. JNI and Kotlin wipe temporary
  key/plaintext arrays where possible. Display strings/protobuf objects and
  runtime/compiler copies cannot be guaranteed wiped.
- Keystore AES-GCM wraps per-installation X25519 secrets and private drafts.
  Binding includes endpoint, server instance and authenticated user. Identity
  corruption is not silently replaced, and desktop private keys are never copied.
- Device directories persist first contact and changed-key warnings. Safety-number
  verification is serialized with directory updates and checks the displayed
  number before accepting. Any participant without keys blocks private sending.
  A self-key removed from the directory is not republished on reconnect.
- Pending operations keep exact encrypted bytes/UUIDs; current directory mismatch
  blocks stale recipient wraps rather than re-encrypting an ambiguous retry.
- Private message/outbox Room data is ciphertext. Private drafts use wrapped files;
  decrypted message bodies never replace cached wire bytes. Unknown/removed sender
  keys are withheld unless exact ciphertext and message context match a retained
  local authentication receipt (see the 2026-10-02 continuation below). Unseen
  revoked-sender history remains unavailable; receipts are observations, not
  historical signing-time or verified-identity proofs.
- Temporary files are app-private, bounded and cleaned after failures/process start.
  Export starts only after digest, final tag and sealed size checks. Document
  providers can still leave partial output after a provider/write failure.

## Dependency provenance

Official [libsodium installation guidance](https://doc.libsodium.org/installation)
and [Android 16 KiB guidance](https://developer.android.com/guide/practices/page-sizes)
were checked before native integration. NDK r28c/CMake 3.31.6 build static PIC
libsodium and libc++ into the crypto JNI library, excluding desktop dependencies.
The 2026-09-28 libsodium 1.0.22 stable source snapshot is vendored with its upstream
signature; the file signature was verified against the official Ed25519 public
key, and CMake verifies its pinned SHA-256. APK assets carry license notices.
See [dependency manifest](../android/third_party/README.md).

## Executed evidence

- Server/desktop CMake build and **190/190** tests (126 unit, 58 integration, 6 fuzz).
  Added file-boundary and existing-destination-preservation coverage.
- Emulator combined workflow: **10/10 tests/phases**, including the six prior
  community/reliability phases plus four crypto phases. Real isolated server and
  desktop daemon verify text/replies/edits/files in both directions, groups,
  matching safety numbers and server rows containing ciphertext with empty content.
- The four crypto phases also passed against the installed **unmodified desktop
  v0.2.2** daemon, avoiding reliance solely on two callers of the extracted core.
- Crypto tests cover malformed inputs, context changes, tampering, nonrecipients,
  secretstream boundaries, changed-key blocking, stale-outbox rejection,
  verification invalidation, persistent warnings and self-revocation surviving
  reconnect. Force-stop restores the same installation key, cached private history
  and wrapped draft, including offline restoration before networking.
- Debug and unsigned R8 release builds; protocol JVM tests and lint; both ABI
  libraries and all other packaged native libraries pass ELF/APK 16 KiB checks.

## Encryption-device controls (2026-10-02)

Settings now lists own-account encryption keys with full SHA-256 fingerprints,
registration timestamps and local-installation identification. Confirmation is
required before revocation. The crypto coordinator refreshes the own directory,
rejects keys absent from it, submits the existing account-scoped protocol operation
and confirms removal through a fresh directory read. Self-revocation preserves the
local secret for old wraps while disabling new sends and automatic republishing.
Key revocation does not terminate login sessions; their management remains absent.

Verified on API 36 emulator: real Settings navigation, fingerprint display and
cancelled self-revocation; extra own-account key revocation; foreign-key rejection;
self-revocation preserved across reconnect with no automatic republishing. All four
crypto fixture phases still pass, including desktop↔Android files/groups/edits and
force-stop recovery. Physical-device revocation/UI workflows remain unverified.

## Remaining gaps

History trust for revoked sender devices, transfer resume and persisted upload state
remain incomplete. Remote login-session controls now exist on capable servers (see below). Foreground transfer progress and
cancellation now exist, with five initial worker/cancellation/export-failure regressions
and one complete 17-phase emulator workflow (see implementation status). Injected
mid-write stream failure is not exhaustive SAF provider validation. Blocking provider
I/O and synchronous native crypto remain cancellable only at their next checkpoint. Community
cache is app-private plaintext. Multi-account, native voice/media, OAuth, push,
share flows and full accessibility/device testing remain separate milestones.
There is no external audit or 16 KiB runtime/physical arm64 validation. Existing
Ordering now has separate deterministic Room regressions for delayed acknowledgements,
history/event edits/deletions, restart and cache replacement. Android socket-write
fault injection and exhaustive concurrent-network timing remain unverified.

## Login-session controls (2026-10-02)

Remote login-session management now exists independently of encryption-key controls.
Account-scoped server deletion invalidates every access grant and refresh credential
for the selected login, closes its connections and removes its owned voice lease.
Encryption keys and other logins remain. Android Settings confirms revocation and
routes current-session removal through local sign-out. A revoked coordinator requires
login again while retaining its independent identity and account draft/cache.
Capability-gated lists include no credentials, digests or network addresses.

CTest 195/195 and the complete emulator workflow 18/18 passed. Actual Compose
confirmation/cancellation, revoked access/refresh rejection, retained directory key,
remote coordinator revocation and re-login with the same identity/draft are tested.
Historical sender trust, transfer resume, SAF hardening, audit and later milestones
remain incomplete. See implementation status for exact evidence and limitations.

## Exact-byte historical sender provenance (2026-10-02)

A live directory observation plus successful native decryption can now produce a
Keystore-wrapped local receipt, bound to the account/server scope, message ID,
author, channel and exact ciphertext. Up to 1,024 observations are retained, with
local dates; display performs native authentication again. Removed sender keys can
open only matching retained observations. This records local trust on first use,
not an authenticated historical signing time, key transparency or necessarily a
verified identity. The UI says the sender is no longer registered and the identity
was not necessarily verified. Current-directory send/retry guards are unchanged.
Offline directories cannot produce receipts. Unseen history/new edits, missing
wraps, tampering and changed context remain unavailable. Corrupt/evicted receipts
fail closed without rotating keys; sign-out removes the journal. The receipt and
Room message are separate stores: a crash can leave an unused receipt, which carries
no plaintext and only authorizes its exact previously authenticated bytes.

HistoricalSenderTest uses a real live directory and revocation to check exact-byte
fallback, unseen messages/edits, tampering/context/IDs, explicit account checking,
fresh offline coordinator restoration, 1,024-entry eviction, scope isolation,
malformed wrapped provenance and identity preservation. Direct Compose provenance
label tests and actual process death specifically with a removed sender receipt
remain unverified. See implementation status for final run results. No independent
crypto audit or general revoked-sender historical directory is claimed.
