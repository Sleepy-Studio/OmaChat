# Android implementation status

Updated 2026-10-02. Branch: `feature/android-client`. This file records evidence,
not release readiness. No production deployment, publishing, or signing performed.

New session: start with [session handoff](android-session-handoff.md).
Current priority: voice routes/interruption and physical validation, then persisted
resumable transfers and the remaining mobile milestones.

## Inspection and baseline

Source inspected: README, architecture/protocol/media/security/security roadmap,
development status, shared schema, authentication/cleanup/message/storage/voice
handlers and relay, daemon connection/E2E/audio/media, CMake/tests/packaging/CI.
Baseline server schema was 13; this working tree uses schema 14. Some older status paragraphs still say 7.
Flux checked out at `/tmp/omachat-flux-reference`; Android organization, Gradle,
Compose application lifecycle, services, docs, build and release workflows reviewed.
Flux has **no selected license** (its AGENTS.md confirms this). No Flux application
source is reused. Gradle wrapper comes from upstream Gradle, under Apache 2.0.
Unrelated `session-ses_f0f5.md` is preserved.

Baseline: `cmake --build build -j4` passed. `ctest --test-dir build -j4
--output-on-failure` passed **185/185** with loopback permission (125 unit,
54 integration, 6 fuzz). Initial sandbox execution could not bind sockets;
that is an environment failure, not a source regression. Existing synthetic
voice baseline passed; physical two-device voice is unverified.

## Implementation sequence

1. Direct TLS trust/login/channel/history/text exchange; shared generated protobuf.
2. Durable send operations, Room cache/drafts/outbox, read markers, reconnect and
   full synchronization. Reconcile history separately from structure.
3. Extract reviewed E2E operations, installation keys and trust UI, encrypted files.
4. Session voice ownership, bounded native media/audio and foreground lifecycle.
5. Multi-account/OAuth/push and authenticated notification actions.
6. Stream viewing, accessibility/tablets, native page alignment, release checks.

## Implemented and verified

Native Kotlin/Compose app and generated shared protobuf modules connect directly
through TLS 1.3. Implemented username/password login, explicit certificate trust,
channels, paginated history, real text sends/replies/edits/deletion/reactions,
Room drafts/cache/outbox and read markers, protected tokens, bounded reconnect,
serialized refresh/lifecycle teardown and offline cached structure. Activity
recreation retains the application coordinator. Frame/request/event queues are
bounded; the full-sync wire barrier prevents replay congestion from blocking replies.

Server schema 14 adds atomic stable-ID sends, conflict checks and deleted-message
tombstones, durable per-account read markers, persistent instance UUID and explicit
session voice ownership. Former-owner controls and delayed disconnect cannot end a
transferred lease. Desktop integrates visible focused read markers and stops media
on ownership loss. All changes are additive and capability advertised. No hosted
server deployment or migration was performed.

Prior executed results (2026-10-01); latest continuation is recorded below:

- CMake build passed; CTest **190/190** passed (126 unit, 58 integration, 6 fuzz),
  including the real 16-second delayed voice-disconnect test and new portable
  attachment boundary/atomic-output tests.
- Android shared-protocol JVM tests **4/4** passed. Debug build, lint (0 errors, 15 warnings) and
  R8 optimized unsigned release build passed. CI jobs are added but remote workflow
  execution is not yet verified. Existing desktop/server jobs remain intact.
- API 36 x86_64 emulator, isolated real TLS server and null-audio desktop daemon:
  **10/10 instrumentation tests/phases passed**, followed by independent desktop history
  verification. Tests cover exact certificate confirmation, real login/channels,
  desktop↔Android messages, Room draft/outbox, reconnect after 100 offline events,
  paginated recovery and short-resume preservation of older pages, Activity recreation,
  migrations 1/2/3→4, account isolation, interrupted sends, secret wrapping and tamper
  rejection. A 620-row Room Paging test loads beyond the former 500-row cutoff.
  Separate seed/recovery phases with a real force-stop verify a new PID, restored
  wrapped credentials/draft/channel/payload and explicit retry of an Uncertain send.
- Before unplugging, Samsung SM-S908U Android 16/API 36 passed real isolated LAN
  desktop↔phone exchange and an explicitly authorized hosted-server test through
  public DNS at omachat.sleepystudio.xyz. The test verified the operator's existing
  certificate fingerprint, created real disposable users, joined the authorized
  invite, sent a marked test message and independently fetched it. Hosted v0.2.2/
  protocol 1.2 lacks new capabilities; basic community text compatibility passed.
  The phone retains a prior working debug build/test login. Latest changes are
  emulator-tested only; the phone was unplugged during that run; see the 2026-10-02 continuation below.

Local logs: `/tmp/omachat-regression-tests.log`,
`/tmp/omachat-emulator-integration.log`, `/tmp/omachat-android-release-check.log`,
`/tmp/omachat-phone-integration.log`, `/tmp/omachat-hosted-phone-direct.log`.
Logs are temporary artifacts, not version-controlled test evidence.

## Milestone 3 evidence (2026-10-01)

The combined emulator workflow now includes four encryption phases in addition
to the prior six tests/phases. Native crypto rejects tampering, wrong channel/author,
nonrecipients and malformed input; files cover empty/exact-64-KiB/boundary/trailing
cases. A fixed independently computed safety vector guards serialization.

A real disposable TLS server and desktop daemon exchange encrypted text, replies,
edits, 200,000-byte desktop→Android and 131,072-byte Android→desktop files, and group
messages. Server message rows have empty content and ciphertext. Safety numbers
match with distinct device keys. Adding/removing a peer device invalidates trust,
blocks sends/stale outbox wraps and persists warnings across force-stop. Removing
Android's own key is not undone on reconnect. Offline restart restores ciphertext
history and Keystore-wrapped private draft without a network connection.

All four crypto phases also passed with `/usr/bin/omachatd` **unmodified v0.2.2**,
which retains the original crypto implementation. Final debug/unsigned R8 release,
4 protocol tests and lint (0 errors/15 warnings) passed. All six packaged native
libraries per APK (crypto plus AndroidX dependencies, both ABIs) pass ELF/APK 16 KiB
alignment. A prior documentation claim that no native dependency was packaged was
incorrect: AndroidX libraries were present. Live SDK inspection also corrected
build-tools to **36.0.0**; Gradle/CI now pin it. NDK r28c and CMake 3.31.6 were installed.

Pinned libsodium 1.0.22 stable snapshot signature and SHA-256 verified; source and
license notices are included. No desktop secrets or Flux application source reused.
Logs: `/tmp/omachat-crypto-final-emulator.log`, `/tmp/omachat-crypto-legacy-final.log`,
`/tmp/omachat-crypto-regression.log`, `/tmp/omachat-crypto-lint.log`,
`/tmp/omachat-crypto-alignment.log`. Temporary logs are not durable test artifacts.

## Validation failures corrected

Initial socket restrictions were environmental. A blocked LAN port was replaced
with an existing allowed unused port, without firewall changes. Gradle connected
instrumentation originally uninstalled the phone app; the app and hosted login were
restored using direct instrumentation. The local fixture script now preserves app
installation. A development Room schema mismatch was corrected with migration 2→3
and tested using existing emulator data. The stress test respects server rate limits;
desktop validation now paginates past 100 newer messages. These failed attempts are
recorded separately from the subsequent successful runs. During Paging integration,
an empty snapshot caused an Activity recreation crash; a guarded immutable snapshot
read fixed it and the full emulator workflow passed afterward. Concurrent release KSP
and debug lint generated-source discovery raced; sequential build then lint passed.
The documented commands and CI now use those separate invocations. During crypto testing, inferred coroutine
return types failed JUnit void-method validation; explicit Unit fixed them. adb shell
split a safety number on spaces; argument quoting fixed it. A fixture wrongly
expected desktop's opaque random attachment name to be literal `file.enc`; the
assertion now checks that the real filename stays hidden. A crypto-only run also
needed explicit initial certificate confirmation. Corrected runs subsequently passed.

## Milestone status and next work

1. **Real connectivity: working community-text vertical slice.** Production UI
   account registration/invite joining remain missing; community/private file picking and export are implemented.
2. **Reliability: partial.** Stable payload/operation persistence, uncertain delivery,
   explicit safe retries, read markers, full reconciliation and migration work.
   Short resume applies up to 32 replay events to the persisted snapshot/history.
   Larger/unavailable replay falls back to full sync; old history is then discarded
   and selected-channel pages fetched independently. Room/Compose Paging removes
   the 500-message UI cutoff with 50-row pages and a 300-item cache target.
   A real TLS lost-response test discards an accepted acknowledgement before caller
   delivery and verifies retry returns one original message; a pre-transmission
   disconnect case also passes. Real force-stop/relaunch persistence is tested.
   Fully reconciled deep offline archives, Android-specific loss during an in-flight
   socket write, bounded workers and full cross-device read UI validation remain.
3. **Private conversations: working vertical slice, hardening incomplete.**
   Qt-free message/file/safety operations are shared with desktop through bounded
   JNI. Independent Keystore-wrapped Android device keys publish to the directory;
   DMs/groups/replies/edits/files interoperate with current desktop and unmodified
   v0.2.2. Safety numbers match; changed keys invalidate verification and block
   sends/stale-outbox retries. Self-revocation survives reconnect without automatic
   republishing. Force-stop restores encrypted cached history and wrapped private
   drafts offline. Encryption-device listing/revocation UI is now implemented. Bounded exact-byte historical sender provenance is now implemented (see continuation below); unseen revoked-sender history, resumable transfers and external audit remain incomplete.
   Remote login-session management is implemented on capable servers (see continuation below). Foreground progress/cancellation and initial export-failure regressions are implemented (see below).
4. **Voice: application integration, physical validation incomplete.** Shared native packet encryption,
   bounded UDP registration/reception, replay/nonce guards and fixed jitter queues
   now feed desktop-compatible Opus, AudioRecord/AudioTrack, bounded mixing and PLC.
   Explicit joins/transfers, permission/UI controls, audio focus and microphone
   foreground service are implemented. Routes, physical audio and broader interruption
   validation remain required; see the latest continuation.
5. **Everyday mobile use: unstarted.** FCM/no-Google variants, optional gateway,
   mobile OAuth, multiple saved accounts, share intents and notification actions.
6. **Media/hardening: partial binary checks only.** Native ELF/APK alignment
   passes; screen viewing, tablet adaptation, runtime 16 KiB and complete
   accessibility/hardware validation remain.

Community plaintext cache is app-private but not separately encrypted. Only one saved
account exists. arm64-v8a and x86_64 crypto/AndroidX native libraries are packaged;
ELF/APK 16 KiB checks pass for both debug and release. Actual runtime validation is
on a 4 KiB x86_64 emulator; physical arm64 and 16 KiB runtime remain unverified. Background notification delivery is unavailable and idle sockets
close when backgrounded. No fake push, voice or private encryption implementations.
Physical echo/Bluetooth/headset/calls/focus/screen-off battery/latency/cellular checks
are **unverified**. Android E2E interoperability is emulator-verified; physical Android E2E is unverified.
External push/OAuth credentials and production signing are unconfigured. Authorized
hosted test accounts/messages remain real test artifacts; the first account lost its
local login during the Gradle uninstall. Operator cleanup was not silently performed.

Next: close transfer and historical sender-trust gaps, followed by native media/audio,
OAuth/push/multiple accounts and media hardening. Source review and dependency
provenance: [crypto review](android-crypto-review.md).

Exact build/install/run/test and signing commands: [Android guide](android.md).
APKs: `android/app/build/outputs/apk/debug/app-debug.apk` (**debug-signed**) and
`android/app/build/outputs/apk/release/app-release-unsigned.apk` (**unsigned R8 release**).
No commits, pushes, production deployment, publication or credential rotation.
The requested complete application is not yet finished or production-ready.

## Device controls and cache ordering continuation (2026-10-02)

Settings now provides own-account encryption-key listing, full SHA-256 fingerprints,
registration dates, local-installation identification and confirmed revocation.
Revocation refreshes the directory, rejects foreign/unregistered targets and confirms
server-side removal. Self-revocation keeps the local secret until sign-out, disables
new private sends and survives reconnect. This is encryption-key management;
remote login-session termination and device naming remain absent. The Messages tab
now routes to private conversations rather than an obsolete unsupported notice.

Room schema 6 adds durable event mutation revisions/deletion tombstones and an
account/revision index. History replies cannot overwrite events newer than their
request fence or restore tombstoned rows. Original idempotent receipts only insert
untouched missing messages; full cache replacement invalidates in-flight pages and
receipts. Cursor/event commits remain transactional. Deep offline archive recovery,
Android mid-write injection and exhaustive network-race testing remain gaps.

Corrected attempts: expanded version-4 migration fixture initially omitted its
original indexes, then assumed every historical schema had an indices array. The
fixture now recreates optional indexes and covers every schema 1–5. Adding the
mutation index to an already-installed interim schema-5 emulator caused a Room
identity mismatch; migration 5→6 preserves that installed data. The initial UI
selector used Android's text-search API, which returned no Compose virtual nodes;
walking the exposed accessibility tree replaces that lookup.

Verified this continuation:

- CMake build and CTest **190/190** passed.
- Debug and unsigned R8 release builds passed. Protocol JVM **4/4** passed;
  lint **0 errors / 15 warnings**. Both APKs' packaged libraries pass ELF/APK
  16 KiB alignment. Runtime 16 KiB remains untested.
- API 36 x86_64 emulator: all **12 tests/phases** in the expanded workflow passed
  across the community/persistence run (6 tests plus 2 process-death phases) and
  the final crypto-only run (4 phases). A single final combined run was not repeated.
  The six tests include both new cache-ordering regressions and migrations 1–5→6.
  The crypto run covers actual Compose Settings navigation, device fingerprint display
  and cancelled self-revocation, plus real own-account extra-key revocation, foreign-key
  rejection and self-revocation remaining blocked after reconnect. Desktop↔Android
  encrypted text/replies/edits/files/groups and force-stop recovery still pass.
- Samsung was connected in adb inventory but was not installed, cleared or tested.
  New code remains emulator-verified. No commit, push, deployment or signing occurred.

Logs: `/tmp/omachat-android-session-ctest.log`,
`/tmp/omachat-android-session-complete-integration.log` (community/persistence passed;
initial UI selector failed), `/tmp/omachat-android-session-crypto-final.log` (all crypto
phases passed), `/tmp/omachat-android-session-final-variants.log`,
`/tmp/omachat-android-session-lint.log`, `/tmp/omachat-android-session-alignment.log`.
Logs are temporary, not durable test evidence. The original full Android requirements
were not located in repository files; milestone docs are the available scope record.

## Foreground transfer controls continuation (2026-10-02)

Uploads/downloads now show stage-specific progress and byte counts in the app shell,
with cancellation and dismissible terminal outcomes. One transfer worker is allowed
at a time; a second request fails immediately instead of creating a queue. State and
cancellation are application-owned and survive Activity recreation. Sign-out cancels
and joins an active transfer before removing account secrets. Cancellation checks run
between bounded import/export and network chunks, and around native encryption.
Known upload tickets are cancelled using a bounded three-second noncancellable cleanup;
lost tickets or failed cleanup remain subject to the server's existing expiry policy.
Temporary staging and transient native key arrays are cleaned on every exit.

The composer now clears only after the exact message is queued durably. Upload failure
or cancellation retains the current text/reply/file selection while that conversation
remains open. File selection is not persisted across process death. Edits made during
preparation are preserved; draft writes and enqueue-time clearing share the send mutex.
A single composition-preparation lock prevents duplicate concurrent preparation. The
existing regression now verifies that sending different text does not erase a saved draft.

Export still verifies digest and private authentication/size before opening the output.
Community downloads enforce the plaintext 100 MiB cap before export; private metadata
must match the attachment and fit the cap. Saving and terminal error/cancel states warn
that a document provider may leave partial output. Existing documents are not silently
removed after failure. Cancellation cannot preempt a blocked provider syscall or an
in-progress synchronous native crypto operation; it takes effect at the next checkpoint.

Executed validation:

- One complete API 36 x86_64 emulator workflow passed **17/17 tests/phases**:
  11 community/Room/cache/transfer tests, two process-death phases and four crypto
  phases, including real desktop encrypted files/groups/edits and offline recovery.
- Five new transfer tests cover known-ticket cancellation/worker bounds, actual TLS
  server cancellation after one acknowledged 512 KiB chunk (resume returns not-found),
  digest/authentication before output, injected mid-write output failure/stream closure,
  an unavailable content-provider URI, cancellation before output opens, staging cleanup
  and bounded copy/cancellation. The mid-write stream is an injected document-output
  boundary, not a claim of exhaustive real SAF provider testing.
- Debug/test APKs and unsigned R8 release passed; protocol JVM **4/4** passed;
  lint **0 errors / 15 warnings**. Both APKs passed ELF/APK 16 KiB alignment checks.
- No C++/server/protocol changes in this continuation; prior 190/190 CTest evidence
  remains prior evidence and was not rerun. Samsung was not installed or tested.

Corrected attempts: the initial test build called a suspending coroutine-context helper
inside a nonsuspending progress callback; capturing the owning Job fixed compilation.
The first integration attempt stopped before APK installation because the previously
listed emulator disappeared. Restarting the existing AVD and rerunning the entire
workflow passed. No phone fallback, data clearing, uninstall or hosted mutation occurred.
Logs: `/tmp/omachat-transfer-build.log`, `/tmp/omachat-transfer-integration.log`,
`/tmp/omachat-transfer-release.log`, `/tmp/omachat-transfer-lint.log`,
`/tmp/omachat-transfer-alignment.log` (temporary artifacts).

Still missing: resumable/persisted upload and download state, retry across reconnect or
process death, exhaustive SAF/storage/quota failures, direct Compose transfer-control interaction
tests and physical transfer/UI testing. The new UI compiles; the new automated tests
exercise transfer/coordinator boundaries rather than clicking its progress/cancel controls.
Historical revoked-sender trust, remote login sessions, Android native voice/media,
OAuth/push/multiple accounts/share flows and the rest of the full milestones remain.
Original full requirements have still not been supplied; the existing milestone docs
are the available scope record. All work remains uncommitted on feature/android-client;
no deployment, publication or production signing. The full Android app is unfinished.

## Remote login-session continuation — 2026-10-02

Implemented Settings **Login sessions** with observed connected/current flags,
creation and refresh-expiry dates, session IDs, confirmed remote revocation and
bounded 100-row pages (**Older sessions** replaces the current page). Current-login
removal stays under local Sign out. Offline lists are explicitly snapshots. Device
names/addresses are unavailable; keys and sessions are distinct controls.

New additive `sessions.manage.v1` capability and Envelope fields 159/160/161 list
only this account's unexpired login rows and revoke an account-scoped session.
Database schema remains 14; Room remains 6. Durable deletion invalidates all access
grants for the session, closes all its control connections and immediately tears
down its owned voice lease. Other logins and registered encryption keys remain.
Resume now checks the durable session row. Fixed LogoutRequest to invalidate older
access grants from the same session rather than only one connection's token.
Missing and foreign session IDs both return NOT_FOUND; invalid IDs/cursors and
unauthenticated requests are rejected. No tokens/digests/peer addresses enter lists.
No hosted capability deployment was performed; legacy-server controls are disabled.

Validation completed in this continuation:

- CMake build and CTest **195/195** (127 unit, 62 integration, 6 fuzz).
  New regressions cover scoped pagination/expiry and revocation across database
  reopen, all old/rotated access grants, dead refresh, foreign-session rejection,
  immediate owned-voice teardown, preserved other voice owner, self revocation,
  logout invalidation, unauthenticated calls and invalid IDs/cursors.
- One complete isolated API 36 x86_64 emulator workflow **18/18 tests/phases**
  (12 ordinary, two process-death, four crypto). The new LoginSessionTest exercises
  actual Compose Settings navigation, cancellation and confirmed revocation,
  preservation of the other installation's registered encryption key, current-login
  API guard, remote revocation of the coordinator, LoginRequired recovery, and
  password re-login with the same encryption identity and saved community draft.
  Existing community/E2E/transfer/persistence/desktop interoperability tests pass.
- Debug/instrumentation and unsigned R8/resource-shrunk release builds passed;
  shared-protocol JVM **4/4**; lint **0 errors, 15 warnings**; both APKs pass all
  ELF/APK 16 KiB alignment checks. No new external dependencies or permissions.

The first emulator attempt stopped before installation because the existing AVD
had disappeared. Restarted that AVD; the full workflow subsequently passed.
Temporary evidence: `/tmp/omachat-sessions-build-final.log`,
`/tmp/omachat-sessions-ctest-final.log`, `/tmp/omachat-sessions-gradle.log`,
`/tmp/omachat-sessions-emulator-final.log`, `/tmp/omachat-sessions-lint.log`,
`/tmp/omachat-sessions-alignment.log`; these files are not durable test artifacts.

Remaining gaps: persisted/resumable upload/download journal and retry UI, broader
SAF/quota failures, historical revoked-sender trust with provenance, Android
mid-write loss, deep archive reconciliation and complete read-marker UI validation;
then native Android voice/media/foreground service, push/gateway variants, OAuth,
multiple accounts, share flows, registration/invites, search/typing/mentions/activity,
stream viewing and tablet/accessibility/hardware validation. Session paging above
100 rows is store-tested and UI build-verified; direct older-page interaction and
ambiguous revocation response UI tests remain unverified. Session lists have no live
change event. There is no independent crypto audit, production signing or complete
Android release. Original full requirements remain absent from repository/vault;
existing milestone docs remain the available scope record.

All prior uncommitted work is retained on `feature/android-client`;
`session-ses_f0f5.md` is checksum-unchanged. Samsung was attached but untouched.
No commits, pushes, hosted data changes, production deployment or publication.
Next: persisted resumable transfers and historical sender trust, then native media.

## Samsung update and lifecycle fix — 2026-10-02

At the user's request, installed the latest debug APK on the attached Samsung
SM-S908U using targeted `adb install -r`, preserving the installed application's
data. Initial cold launch succeeded, but a subsequent background transition crashed
with NetworkOnMainThreadException in Conscrypt SSLSocket.close from
SessionCoordinator.foreground → OmaChatApp.onStop. This was a real physical-device
failure; initial launch success alone did not establish a working update.

Removed the synchronous main-thread close from foreground(false). The existing
I/O coordinator lifecycle path now performs stopLoop's socket close before worker
cancellation. MainThreadLifecycleTest observes the actual established-socket close
thread and StrictMode across three disconnect/reconnect cycles. A first StrictMode-
only emulator test passed even with the bug (emulator Conscrypt close did not expose
network I/O); adding the close-boundary observer made it fail on the exact main-thread
close. The same focused regression passed after the single-path fix. The disposable
workflow now includes it and supports `--test-class` for focused fixture regressions.

Reinstalled the fixed APK in place on Samsung. Verified a successful cold Activity
launch, background transition to Home and foreground recovery in the same process,
with no fatal crash for that PID. SHA-256 of installed base.apk matches the fixed
local debug APK. No uninstall, data clear, scripted account login, test-message send or
instrumentation was performed on Samsung. Physical E2E/media workflows remain unverified.
Debug/instrumentation and unsigned R8 release builds passed; lint remains 0 errors /
15 warnings; both APK alignment checks pass. C++/server/protocol code was unchanged;
the preceding CTest 195/195 and protocol 4/4 evidence remains applicable.

Remaining full Android gaps are unchanged. Prior work and session-ses_f0f5.md remain
preserved; no commit, push, hosted deployment or production release.

Final combined emulator validation for this lifecycle fix: **19/19 tests/phases**
(13 ordinary, two process-death, four crypto). The first combined attempt timed out
because a delayed ProcessLifecycle stop from the preceding test interrupted the new
Activity-less regression. Keeping ActivityScenario alive throughout that test fixed
its lifecycle fixture; no additional production change was made. The full rerun
passed. Logs: `/tmp/omachat-main-thread-red-observed.log` (expected main-thread close
failure), `/tmp/omachat-main-thread-green.log` (focused pass),
`/tmp/omachat-samsung-regression-final.log` (complete pass),
`/tmp/omachat-samsung-build.log`, `/tmp/omachat-samsung-lint-final.log` and
`/tmp/omachat-samsung-alignment.log`. Samsung's installed APK was rechecked against
the final debug artifact and its process remained alive without a fatal crash.

## Historical sender provenance continuation — 2026-10-02

Implemented bounded exact-byte local authentication receipts for private messages.
MessageCache calls the crypto observer only for accepted events/history/send receipts;
revision fences and tombstones reject stale replies before provenance is recorded.
The observer explicitly checks the account binding, requires a live-observed sender
key and successful native decryption, and writes on Dispatchers.IO. Display uses
memory and never writes provenance. No Room/protocol/server migration or dependency
change was made.

The Keystore-wrapped journal is scoped to endpoint/server instance/account and stores
only SHA-256 digests of message ID/channel/author/exact ciphertext plus local observation
time. It retains the latest 1,024 distinct observations. After sender-key removal,
matching bytes can still decrypt, with a warning and observation date in the message
UI; no historical signature time or verified identity is implied. Unseen revoked-key
messages, new edits, changed context/IDs and tampering stay unavailable. Offline
cached directories cannot mint receipts. Corrupt/evicted provenance fails closed;
identity material is not replaced. Local sign-out erases the journal. The journal
can outlive Room cache replacement but cannot reconstruct missing message rows.

This is a bounded historical-display policy, not a general historical-key directory.
Previously cached messages need a live re-fetch while the sender key is still
registered to obtain receipts. Missing recipient wraps cannot be repaired. Direct
Compose warning/date interaction and a real force-stop specifically with a removed
sender's receipt remain unverified; fresh-coordinator offline disk restoration is
tested. Provider/transfer resume, SAF/quota failures, Android mid-write loss, deep
archive reconciliation, complete read-marker UI tests and native voice/media/mobile
milestones remain incomplete. Original full requirements remain unavailable in the
repository/vault; milestone docs remain the available scope record.

Final validation: one complete isolated API 36 x86_64 workflow passes **20/20 tests
and phases** (14 ordinary, two actual process-death phases and four crypto phases).
The dedicated historical-sender test also passed in a focused run. Debug/test and
unsigned R8/resource-shrunk release builds pass; lint is **0 errors / 15 warnings**;
both APKs pass all 12 ELF/APK alignment checks. The shared-protocol Gradle task was
up-to-date with existing **4/4** results; no protocol code changed. C++/server code
was unchanged, so prior **195/195** CTest evidence was not rerun or claimed as new.
The final combined rerun includes the explicit account guard added after the first
20-phase pass. The initial focused attempt stopped before installation because the
AVD disappeared; restarting the existing AVD allowed the passing runs.

Temporary logs: `/tmp/omachat-history-focused.log` (AVD failure),
`/tmp/omachat-history-focused-final.log`, `/tmp/omachat-history-emulator-final.log`,
`/tmp/omachat-history-release.log`, `/tmp/omachat-history-lint.log` and
`/tmp/omachat-history-alignment.log`. Physical Samsung remained untouched. Branch
`feature/android-client`, all previous uncommitted work and checksum-unchanged
`session-ses_f0f5.md` remain preserved. No commit, push, hosted mutation, deployment,
production signing or publication. Next: persisted resumable transfers, then native
Android media/voice and remaining full mobile milestones; historical unseen-sender
recovery remains a documented limit.


## Native voice transport continuation — 2026-10-02

Implemented an internal native UDP/media transport foundation in `VoiceMedia.kt`,
`MediaJni.cpp` and the existing JNI CMake target. It links the desktop's unchanged,
Qt-free `MediaPacket.cpp` with the already vendored sodium; no new dependency,
protocol/schema migration or permission. Exact desktop AEAD/header/nonce contracts
are preserved. This is relay encryption, not participant E2E voice encryption.

Authentication precedes replay admission. Datagrams/payloads are capped at 1,400/
1,360 bytes, replay state at 128 stream/type windows, and speaker queues at 64
speakers × 64 frames. Fixed jitter uses 60 ms startup, backlog skipping and a
240 ms stall reset. Adaptive jitter, FEC/PLC and mixing remain missing. Serialized
send counters reject exhaustion; a bounded non-evicting 1,024-entry process digest
set rejects reopening a used key/stream. Media leases/keys are not persisted.
Registration retries every 500 ms, times out after 10 seconds, and authenticated
own-stream ACK enables 15-second keepalives. Expiry/failure ends workers; opening
cancellation closes sockets and stop joins workers/wipes the transport key.
Runtime/protobuf key copies cannot be guaranteed wiped. Saturated stream bounds
fail closed; reconciliation with live voice participants remains needed.

**Voice is still unavailable in the application UI.** No Android microphone capture,
Opus encoder/decoder, AudioTrack playback, foreground microphone service, permission
flow or application ownership-loss/control-disconnect/network teardown exists yet.
The internal caller must supply the authenticated control peer and stop media when
its lease is lost; test code does this explicitly. Do not describe this as working
voice chat or audible desktop interoperability. Four tests verify the independently
serialized packet vector, tampering/bounds, replay/non-wrapping counter/jitter loss,
registration ACK/deadline/expiry/closure/reopen rejection, real bidirectional relay
payloads, ownership conflict/explicit transfer, fresh stream/key and former-owner
leave rejection. Payloads are opaque test bytes, not encoded Opus.

Validation: the corrected complete script passes **24/24 tests/phases** in one
invocation: prior 14 ordinary + two actual process-death + four desktop crypto
phases, then four voice tests against a second fresh disposable server. Focused
voice 4/4 also passed before the final cancellation/expiry refinements; the final
combined run covers those refinements and a deterministic timeout clock fixture.
CTest rerun passes **195/195** (127 unit, 62 integration, 6 fuzz). Debug/test and
unsigned R8/resource-shrunk release builds pass, lint **0 errors / 15 warnings**,
and all 12 ELF/APK alignment checks pass. Protocol task reused existing **4/4**
results; no protocol source changed. Remote CI and runtime 16 KiB remain unverified.

Corrected failures: initial fixture build used the wrong invite request symbol
(fixed to shared JoinInviteRequest); initial emulator disappeared before installation
(existing AVD restarted); the first combined run passed 18 ordinary + process-death
+ crypto-native tests but then exceeded the server's per-IP registration limit.
Voice now uses a second fresh fixture after the original complete workflow, without
weakening production limits or relying on rate-limit refill timing. No source
regression in existing crypto was found. Temporary evidence:
`/tmp/omachat-voice-focused-final.log`, `/tmp/omachat-voice-emulator-final.log`
(failed combined attempt), `/tmp/omachat-voice-emulator-combined.log` (complete pass),
`/tmp/omachat-voice-ctest.log`, `/tmp/omachat-voice-build-final.log`,
`/tmp/omachat-voice-lint-final.log`, `/tmp/omachat-voice-alignment.log`.

Next: Opus and native Android capture/playback, explicit voice-session owner controls
and foreground microphone lifecycle. Physical audio/routes/echo/calls, screen-off,
battery/latency and handover remain unverified. Persisted transfer resume, SAF/quota
failures, unseen historical sender recovery, Android mid-write loss, deep archive/
read-marker reconciliation, push/gateway variants, OAuth, multiple accounts, share,
registration/invites, search/typing/mentions/activity, stream viewing and tablet/
accessibility/hardware validation remain required. Full original Android requirements
remain absent from available repository/vault records; existing milestones are the
scope record. Samsung was attached but untouched. All previous work stays on
`feature/android-client`; `session-ses_f0f5.md` retains its initial SHA-256. No commit,
push, hosted mutation, production signing, deployment or publication.

## Native Opus/audio continuation — 2026-10-02

Implemented an **internal** desktop-compatible 48 kHz mono/20 ms Opus audio layer.
`OpusJni.cpp` reuses desktop's unchanged Qt-free `OpusCodec.cpp`; vendored upstream
Opus 1.6.1 is hash-verified against the upstream download page and statically linked
into the existing JNI library for both ABIs. BSD notice is packaged. No detached
signature or independent source audit is claimed. Native handles are bounded at 256,
use IDs rather than pointers, and serialize lookup/use/destruction. Encode requires
960 finite normalized samples; decode requires one 20 ms packet (max 1,275 bytes),
or explicit null for PLC. Stale handles, wrong codec kind and bad bounds fail closed.
R8 keeps NativeOpus JNI names; final release mapping was inspected.

`VoiceAudio.kt` adds guarded AudioRecord/AudioTrack with communication attributes,
platform AEC when available, partial nonblocking I/O, input/output stall/error guards,
non-wrapping timestamps, mute suppression and 20 ms playback pacing. A bounded
64-stream mixer decodes/sums/clamps and uses PLC only on active missing jitter ticks;
startup/idle never creates continuous PLC. Idle decoders are released. Explicit stop
joins workers before device release; immediate cancellation and device failure clean
up codecs/media/device. RECORD_AUDIO is now declared. No other permission, protocol,
server/Room migration or desktop source edit is added by this continuation.

**Voice remains unavailable in production UI.** Runtime permission flow, microphone
foreground service, audio focus/routes and application voice ownership/control-peer
binding/ownership-loss/disconnect/network/sign-out teardown remain next. The internal
worker must be started only after an explicit authorized join and stopped by its
future owner. This is relay-encrypted media, not participant E2E voice. Fixed jitter
remains; adaptive jitter/FEC scheduling and participant-stream reconciliation remain
missing. No audible two-device or physical echo/route/call/screen-off/battery/latency/
network-handover validation is claimed. Emulator AudioRecord can provide silence.

Validation: focused VoiceAudioTest **4/4** passed. The final complete isolated API 36
x86_64 workflow passed **28/28 tests/phases** in one invocation: prior 20 community/
persistence/crypto phases plus eight voice tests on a second fresh server. Existing
relay tests now exchange and decode actual Opus frames in both directions and after
explicit ownership transfer, with former-owner shutdown still performed by the test.
Independent installed host libopus generates a desktop reference decoded on Android
and decodes Android's reported 440 Hz packet, checking energy/dominant frequency.
Other tests cover JNI duration/array/NaN bounds, stale handles/allocation cap, mixing/
clipping/PLC/idle release, injected partial/zero I/O and device error, decoded remote
playout through the worker, immediate stop and actual emulator AudioRecord/AudioTrack
start/stop. Synthetic/reference tests establish codec meaning, not physical audibility.

Debug/test and final unsigned R8/resource-shrunk release builds pass; lint remains
**0 errors / 15 warnings**; all **12** ELF/APK alignment checks pass. Protocol task
reused existing **4/4** results (unchanged source). CTest rerun passed **195/195**
(127 unit, 62 integration, six fuzz). Runtime 16 KiB and remote CI remain unverified.
Initial native build failed because desktop expects installed opus/opus.h while
upstream build-tree headers lack that prefix; CMake now provides generated compatible
headers. First focused fixture lost its AVD before installation; second reached it
before boot completed. Restarted the existing AVD and waited for boot; focused and
complete runs passed. Final dependency is 1.6.1 after checking current upstream stable.
Temporary logs: `/tmp/omachat-audio-focused-opus161.log`,
`/tmp/omachat-audio-combined.log`, `/tmp/omachat-audio-ctest.log`,
`/tmp/omachat-audio-variants.log`, `/tmp/omachat-audio-release-final.log`,
`/tmp/omachat-audio-lint.log`, `/tmp/omachat-audio-alignment.log`.

Next: explicit application voice ownership, foreground microphone-service lifecycle
and permission/UI controls, then focus/routes/interruption validation. Persisted
resumable transfers, SAF/quota failures, unseen historical sender recovery, Android
mid-write loss, deep archive/read-marker reconciliation, push/gateway variants, OAuth,
multiple accounts/share, registration/invites, search/typing/mentions/activity, stream
viewing and tablet/accessibility/physical hardware remain required. The original full
Android requirements are still absent from available repository/vault records; the
milestone documents remain the available scope record. This is not a finished release.
All previous work remains uncommitted on `feature/android-client`; unrelated
`session-ses_f0f5.md` retains SHA-256
`a2045a0cfd9345015cb9716dd722abf22919391c55fbe0a4659c1cca727662d3`.
No physical-device installation, commit, push, hosted mutation, deployment,
production signing or publication was performed.


## Application voice ownership/foreground continuation — 2026-10-02

Voice channels now expose native joins on `voice.ownership.v1` servers. Compose
requests microphone permission, offers explicit Move voice here confirmation after
ownership conflict, and provides a persistent call bar with Mute/Unmute/Leave.
Notification permission is optional; the non-exported microphone foreground service
has an ongoing notification with Leave. There is no push implementation. Voice is
relay-encrypted, not participant E2E; the UI says the server can access audio.

VoiceCall binds UDP to the authenticated TLS socket's selected peer address, waits
for registration and confirms the user/session/channel/stream before allocating
service audio focus and Android devices. Join-snapshot sequence fences reject old
leave/ownership events and structural replies before they can end a new lease.
Mute/deaf/moderation flags are installed before workers start. Audio focus loss
ends the call, including transient/duck requests; platform communication mode uses
default routes and restores the prior mode. Explicit route selection/SCO/noisy-route
handling, deafen/participants/speaking UI and physical interruption checks are missing.

The application preserves the control connection while a call is backgrounded.
Leave, ownership loss/eviction, control loss, default-network change, sign-out,
service destruction, media expiry/failure and audio failure stop local resources.
Idle background control closes after call termination. Tokens, keys and leases are
not persisted; no sticky/boot/background microphone start or automatic rejoin exists.
Generation tokens reject stale service/failure callbacks. Staged joins are cancellable
before audio allocation. Ambiguous unacknowledged joins close their control owner;
known leave cleanup is bounded and disconnect cleanup can still be delayed server-side.
Every queued foreground-start command promotes before rejection/stop, including one
arriving at a closing service instance. API 29 uses the legacy promotion overload;
API 30+ declares microphone type. Runtime evidence remains API 36 only.

Final validation: **32/32 tests/phases in one complete isolated workflow** (14
ordinary + two real process-death + four desktop crypto phases, then eight native
transport/audio tests and four application ownership tests on separate fresh servers).
VoiceOwnershipTest covers actual application/service joins, conflict/cancel/explicit
transfer, Compose confirmation/mute, former-owner shutdown, no implicit rejoin,
background control, notification Leave, network teardown foreground/background,
sign-out, injected audio-focus callback, service destruction, immediate queued-start
cancellation/watchdog and stale event/snapshot fences. Focus injection is not a real
telephone-call test. The prior host-libopus bidirectional reference and actual emulator
AudioRecord/AudioTrack start/stop tests still pass; emulator PCM can be silent.
No audible physical/desktop-daemon two-device interoperability is claimed.

Debug/test and unsigned R8/resource-shrunk release builds pass; protocol JVM **4/4**;
lint **0 errors / 15 warnings**; all **12 ELF/APK alignment checks** pass. No native,
C++/server, shared schema or database migration/dependency changes were needed in this
continuation. Prior **195/195 CTest** evidence applies to unchanged C++ sources; it
was not rerun or claimed as a new result. Remote CI and runtime 16 KiB remain unverified.

Corrected failures: the first build used an Int for float fill; a test tried a hidden
AudioFocusRequest getter (now invokes the retained actual listener); initial AVD
loss prevented installation and the existing AVD was restarted. A real queued-start
race produced ForegroundServiceDidNotStartInTimeException; promotion/stop of stale
commands fixes it and the watchdog regression passes. Combined fixtures exceeded
production login/registration limits; transport/audio and ownership now use second/
third fresh servers, and ownership uses distinct disposable users, with limits
unchanged. A Compose click attempted a temporarily disabled Mute node before its
accessibility update; the fixture now waits for an enabled actionable node. Failed
runs are not counted as passes. The final full rerun includes all source refinements.

Temporary evidence: `/tmp/omachat-voice-owner-combined-verified.log`,
`/tmp/omachat-voice-owner-release-final.log`, `/tmp/omachat-voice-owner-lint-final.log`,
`/tmp/omachat-voice-owner-alignment.log`. Earlier failures are in
`/tmp/omachat-voice-owner-focused.log`, `/tmp/omachat-voice-owner-ui-final.log`,
`/tmp/omachat-voice-owner-combined.log`, `/tmp/omachat-voice-owner-combined-final.log`
and `/tmp/omachat-voice-owner-combined-pass.log`. These logs are temporary.

Remaining voice gaps: physical sound/echo, device routes/Bluetooth/headsets/noisy
routes, real calls/focus interruptions, screen-off/battery/latency, network/cellular
handover, denied/revoked microphone and notification UI, adaptive jitter/FEC and live
participant-stream reconciliation. Background/network policy is teardown, not seamless
handover. Transfer resume/persisted journals, SAF/quota failures, unseen historical
sender recovery, Android mid-write loss, deep archive/read-marker reconciliation,
push/gateway variants, OAuth, multiple accounts/share, registration/invites,
search/typing/mentions/activity, stream viewing, tablet/accessibility/physical hardware
and complete release hardening remain required. Original full Android requirements
remain unavailable in repository/vault records; milestone docs are the available
scope record. **The complete Android application is not finished.**

All prior uncommitted work remains on `feature/android-client`; unrelated
`session-ses_f0f5.md` retains SHA-256
`a2045a0cfd9345015cb9716dd722abf22919391c55fbe0a4659c1cca727662d3`.
Only the existing disposable emulator was installed/tested. No physical-device
installation, commit, push, hosted mutation, deployment, production signing or publication.
Next: voice route/interruption/physical validation and remaining voice UX/hardening,
then persisted resumable transfers and remaining full mobile milestones.


## Samsung voice-build update — 2026-10-02

At the user's request, rebuilt and installed the latest debug voice build in place
on Samsung SM-S908U using targeted adb install -r. Installed base.apk SHA-256 matches
the local APK. Cold Activity launch, Home/background transition and foreground recovery
all succeeded in the same process, with no fatal AndroidRuntime crash for that PID.
App data was preserved; no uninstall, data clear, instrumentation, scripted login,
message send or voice join was performed. Physical audio/routes/calls/screen-off/
handover and physical E2E remain unverified. No commit or hosted deployment.
Build log: /tmp/omachat-samsung-voice-update-build.log (temporary).
