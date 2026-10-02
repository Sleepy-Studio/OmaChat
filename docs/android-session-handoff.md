# OmaChat Android — session handoff

Updated: 2026-10-02. This is the entry point for the next engineering session.
The user reports the current slice works and requests continuation in a new session.
The complete Android application is **not finished**. Continue implementation;
do not replace the remaining work with a proposal or call this a production release.

## Next task — user priority, 2026-10-02

Implement **native Android voice next**. The user explicitly moved voice ahead of
persisted resumable transfers. Start with the existing UDP/Opus media contracts,
Android audio capture/playback, bounded jitter/replay/nonce handling, explicit
voice-session ownership/transfer and foreground microphone-service lifecycle.
Use isolated server/desktop interoperability tests and record physical audio,
route/echo/call interruptions, screen-off and network-handover gaps honestly.
Transfer resume and the other unfinished milestones remain required follow-up work.
This instruction supersedes the older next-task ordering below. An internal native UDP/packet/jitter foundation is now implemented (latest continuation
below). Internal Opus/capture/playback now exists (latest audio continuation);
application ownership, permission/UI and foreground microphone lifecycle are now implemented (latest ownership continuation below); physical audio/routes and broader lifecycle hardening remain required.

## Start here

1. Read the user's original Android implementation requirements and local AGENTS.md
   instructions. Read contributor instructions and inspect `git status` again.
2. Read `docs/android-implementation-status.md`, `docs/android.md`,
   `docs/android-architecture.md` and `docs/mobile-protocol-changes.md`.
3. Preserve the entire uncommitted working tree. Continue on `feature/android-client`.
4. Recheck devices/emulator availability and run the existing isolated workflow.
5. Read `docs/android-crypto-review.md`. Milestone 3 now has real desktop↔Android
   encrypted text/replies/edits/groups/files, independent keys, safety numbers,
   directory-change guards and offline private cache/draft recovery. Remaining
   historical sender-trust limits are documented. Continue native voice/media next
   per the user priority above; persisted transfer resume remains follow-up work.
   Remote login-session controls now exist; read the latest continuation below. Foreground transfer progress/cancellation now exist; persisted resume remains missing.
   Keep community text and encryption slices buildable; integrate frequently.

## Repository and boundaries

Repository: `/home/howie/Documents/Github/HowieDuhzit/OmaChat`.
Primary upstream: https://github.com/Sleepy-Studio/OmaChat.
Branch: `feature/android-client`. All Android/server work is **uncommitted**.
No commits, pushes, production deployment, credential rotation or release publication
were performed. Automatic commits are disabled unless the user asks.

`session-ses_f0f5.md` was an unrelated untracked file before this task. Preserve it.
The modified tracked files and new `android/`, documentation and Android workflow
are this implementation; do not reset or remove them to obtain a clean checkout.

The user authorized testing `omachat.sleepystudio.xyz` and joining an invite supplied
in conversation. Do not deploy server changes or modify existing live user data.
Prefer the isolated local server for automation. Do not put passwords, tokens,
private keys or test login material into this file or diagnostics. The exact invite
is intentionally not repeated in repository documentation.

Phone: previously Samsung SM-S908U, Android 16/API 36. It was unplugged at the user's
request; use an emulator. It retains an earlier debug build/test login. Do not
uninstall it or clear its data. Latest changes are emulator-tested only.
Latest session started the existing `omachat-test` emulator (`emulator-5554`), API 36
x86_64 with 4 KiB pages. Recheck availability instead of assuming it is still running.

## Inspection and baseline already completed

OmaChat README, architecture/protocol/media/security/security roadmap/development
status, `protocol/network.proto`, server authentication/session cleanup/message/
storage/voice/media paths, desktop connection/E2E/audio/reconnect, CMake/tests/CI and
packaging were inspected. Source is authoritative. Baseline CMake build and
185/185 tests passed; restricted socket binding initially failed environmentally.
Current server schema is 14 after this work (baseline 13); Room schema is 6.

Flux reference: `/tmp/omachat-flux-reference`, from https://github.com/bjarneo/flux.
Its Android organization, lifecycle/services, Compose, Gradle and release practices
were reviewed. Flux has no selected license, as its AGENTS.md confirms. **No Flux
application code was copied.** The Gradle wrapper came from upstream Gradle.
The existing shared CMake library depends on Qt and cannot be linked wholesale into
Android. Do not bring PipeWire/keyring/systemd/XDG/IPC/screen capture into JNI targets.

## Actual implementation

### Android files

- `android/app`: native Kotlin/Compose Material 3 UI, application coordinator,
  ViewModel/StateFlow, Room, DataStore, Android Keystore secret wrapping.
- `android/protocol`: generated protobuf-lite from the shared schema, big-endian
  u32 length framing, TLS 1.3, bounded requests and event delivery.
- `SessionCoordinator.kt`: one saved account/connection, serialized lifecycle and
  refresh rotation, reconnect backoff, stable persisted send UUID/payload, explicit
  retry/cancel, offline snapshot, history and read markers.
- `LocalData.kt`: account-scoped Room messages/outbox/drafts/read markers/snapshots;
  migrations 1→2→3→4→5→6, indexed Room PagingSource; AES-GCM Keystore wrapping of tokens
  in atomic no-backup files. Passwords are not persisted.
- `ChatViewModel.kt` / `MainActivity.kt`: community/channel navigation, paginated
  conversation, replies/edits/deletion/reactions, outbox controls and actual visible
  read marking. Room/Compose Paging uses 50-row pages, 300-item page-cache target,
  placeholders and stable keys; no 500-message UI cutoff.
- `OmaChatApp.kt`: application-owned session survives Activity recreation; idle
  sockets close when backgrounded unless an explicitly joined call owns the
  microphone foreground service/control connection. No background push exists.
- `android/tools/integration.py`: disposable real TLS server + separate null-audio
  desktop daemon + Android instrumentation, preserving app installation.
- Tests: `ServerIntegrationTest`, `RoomPersistenceTest`, two-phase `ProcessDeathTest`.
  `HostedServerTest` is explicitly opt-in and excluded from ordinary automated runs.

Trust uses normal platform certificate/hostname validation. Initial self-signed trust
requires exact SHA-256 fingerprint confirmation, only for a valid self-signed leaf.
Saved pins are endpoint scoped and a changed pin stops connection. Explicit acceptance
of a pin authorizes that certificate for the selected endpoint, including hostname.
No global trust-all manager exists. Backups are disabled and screens use FLAG_SECURE.
Community plaintext Room data is app-private but not separately encrypted.

Short resume applies at most 32 complete replay events from Room's transactional
cursor. Longer/unavailable replay performs full sync, clears old cached history and
fetches selected-channel history separately. Structural replay events can perform
full sync and invalidate subsequent replay entries. Event application and cursor
commit together. The TLS reader never blocks on its event queue; overflow closes the
connection for recovery. Full-sync replies act as wire-order barriers.

### Server/protocol/desktop

See `docs/mobile-protocol-changes.md` for exact field numbers and semantics.

- `messages.idempotency.v1`: `SendMessageRequest.operation_id`, exactly 16 bytes.
  Account-scoped operation/digest/accepted-result persisted atomically with message
  and attachment claims. Same retry returns original result; conflicting content
  fails. Records last until account deletion. Deleted messages keep a digest/ID
  tombstone with erased response bytes and cannot be resurrected by retry.
- `read.markers.v1`: authorized durable account/channel read position ordered by
  message `(created_at, id)`, delivered only to that account's devices. Sync includes
  markers. Desktop sends markers only for visible focused newest content.
- `voice.ownership.v1`: owner session/connection, explicit transfer, new relay stream/
  key, preserved moderation, former-owner operation guards and delayed cleanup guard.
  Desktop stops media and clears auto-rejoin intent on ownership loss.
- `instance.identity.v1`: persistent public database-instance UUID in HelloReply;
  Android invalidates local work and requires login when a bound UUID changes.
- CLI `message history` gains additive `--before MESSAGE_ID` for pagination.

Old-server community text worked against hosted v0.2.2/protocol 1.2 without these
capabilities. Ambiguous sends on legacy servers cannot be replayed automatically.
Private sends now require e2e.v1 and current reviewed device keys; no plaintext downgrade.

## Verified results

Earlier completed runs (2026-10-01); latest continuation is recorded below:

- CMake build: passed.
- CTest: **190/190** (126 unit, 58 integration, 6 fuzz), including real delayed voice
  cleanup and lost-response retry. The lost-response test drops an accepted TLS reply
  before delivering it to its caller, aborts the connection and verifies another
  session sees one accepted message and retry returns its original ID. It also tests
  a closed connection before transmission. Android mid-write fault injection remains.
- Shared-protocol JVM tests: **4/4**.
- Emulator API 36 x86_64: **10/10 tests/phases** (the prior six plus four crypto phases). Real desktop↔Android messages,
  100 offline events/full recovery, short-resume preservation of older pages,
  pagination, Activity recreation, Room migrations/isolation, 620-row deep paging,
  secret tampering, changed process PID, restored credentials/draft/channel/exact
  pending payload, Uncertain recovery and successful explicit retry all passed.
- Real encrypted DMs/replies/edits/files/groups and matching safety numbers passed
  against both current desktop and installed **unmodified v0.2.2** desktop. Key
  changes block sending/stale outbox retries, invalidate verification and persist
  warnings after force-stop; self-revocation is not undone on reconnect. Private
  ciphertext and wrapped draft restore offline before networking. New-device old
  history without a wrap is unavailable. See crypto review for exact coverage.
- Debug and R8/resource-shrunk unsigned release builds: passed.
- Lint: **0 errors, 15 warnings**. CI added; remote CI execution is unverified.
- Prior physical LAN and authorized hosted-server tests passed before unplugging.
  Hosted tests created real disposable accounts/messages. One first-run account lost
  its local login because Gradle uninstalled the app; no silent server cleanup occurred.

Temporary logs (may disappear): `/tmp/omachat-regression-tests.log`,
`/tmp/omachat-emulator-integration.log`, `/tmp/omachat-android-release-check.log`,
`/tmp/omachat-extension-build.log`, `/tmp/omachat-phone-integration.log`,
`/tmp/omachat-hosted-phone-direct.log`.

Resolved failures: blocked loopback/LAN socket environment; Room version mismatch;
Gradle connected-test app uninstall; test rate limiting; desktop history query missing
older test messages; empty Paging snapshot crash during Activity recreation. Record
these as corrected failures, not as passes of the initial attempts.

## Reproduce locally

Existing SDK: `/home/howie/Android/Sdk`. Java 21:
`/usr/lib/jvm/java-21-openjdk`. The host's default Java 26 is incompatible with this
pinned build; set JAVA_HOME explicitly. Read SYSTEM_PROFILE before environment changes.

```sh
cd /home/howie/Documents/Github/HowieDuhzit/OmaChat
export JAVA_HOME=/usr/lib/jvm/java-21-openjdk
export ANDROID_HOME=/home/howie/Android/Sdk
export PATH="$ANDROID_HOME/platform-tools:$ANDROID_HOME/emulator:$PATH"

adb devices
# Existing AVD; recheck availability before launching another instance:
ANDROID_AVD_HOME=/home/howie/.config/.android/avd emulator -avd omachat-test \
  -no-window -no-audio -no-boot-anim -gpu swiftshader -no-snapshot
# Run in another shell after boot, with the same environment:
cmake --build build -j4
ctest --test-dir build -j4 --output-on-failure
python3 android/tools/integration.py --serial emulator-5554

cd android
./gradlew :app:assembleDebug :app:assembleRelease
./gradlew :protocol:test :app:lintDebug
adb -s emulator-5554 install -r app/build/outputs/apk/debug/app-debug.apk
adb -s emulator-5554 shell am start \
  -n org.omachat.android.debug/org.omachat.android.MainActivity
```

Toolchain: min API 29, compile/target 37; SDK package `platforms;android-37.0`,
build-tools **36.0.0** (corrected by live SDK inspection); NDK 28.2.13676358,
CMake 3.31.6; Gradle 9.4.1 checksum pinned, AGP 9.2.1, Kotlin/Compose 2.3.10,
Java 17 bytecode, Room 2.8.4, Paging 3.5.1. Full dependencies are in Gradle files.
Run variant builds **before a separate lint invocation**: concurrent release KSP and
debug lint generated-source discovery raced. Commands/CI separate them.

Gradle connected instrumentation can uninstall the target application. Use disposable
emulators; the isolated script uses direct `adb install -r` / `am instrument`.
Do not run ProcessDeathTest as a generic class: seed/recovery must be separate
processes, in order, with force-stop between them. The script implements this.

APKs (generated, ignored):

- `android/app/build/outputs/apk/debug/app-debug.apk`: debug-signed (~38 MB).
- `android/app/build/outputs/apk/release/app-release-unsigned.apk`: unsigned R8 release
  (~3 MB), **not production-signed**.

Release signing accepts all four OMACHAT signing variables documented in android.md;
none have been supplied. arm64-v8a and x86_64 crypto libraries and existing AndroidX
native dependencies are packaged. All ELF/APK 16 KiB alignment checks pass; runtime
16 KiB/physical arm64 testing remains unverified. Earlier claims of no packaged
native dependencies were incorrect because AndroidX libraries were already present.

## Next milestone and remaining work

`shared/src/omachat/crypto/MessageCrypto.*` and `FileCrypto.*` are the Qt-free
operations used by desktop adapters and Android JNI. Kotlin owns key storage,
directory/trust, protobuf and file transfer orchestration. Native dependency is the
signature-verified, vendored 2026-09-28 libsodium 1.0.22 stable snapshot; see
`android/third_party/README.md`. All source review limitations are in
`docs/android-crypto-review.md`; there is no independent audit or forward secrecy.

Remaining crypto/mobile gaps: historical trust for
revoked senders (strict current directory checks can hide their older messages),
persisted resumable transfer state and full failure testing of SAF (foreground progress/cancellation now exist).
The current UI selects one file per composition, bounded at 100 MiB and the server's
lower limit. Export authenticates before touching the destination, but a failing
provider can leave partial output. Private drafts are separately Keystore-wrapped;
community Room data stays app-private plaintext. Offline private history works
using the last-known directory; a new installation cannot read earlier messages
without its own wrap. Sign-out attempts revocation but offline sign-out cannot
confirm server revocation. No silent identity replacement on corrupt storage.

Reliability still needs full read-marker UI synchronization validation, Android
mid-write loss tests, a fully reconciled deep archive after replay is unavailable,
and bounded workers when background synchronization exists. Cache ordering now uses durable event revisions/tombstones, request fences and
insert-only original acknowledgements; full cache replacement invalidates in-flight
pages/receipts. Deterministic Room regressions cover edits/deletes in both arrival
orders, restart, account isolation and cache replacement. Real Android socket-write
fault injection and exhaustive concurrent-network timing remain unverified.

After closing the remaining crypto slice gaps: Android native media/voice and foreground lifecycle using existing
UDP/Opus contracts and explicit ownership; then OAuth/push/multiple accounts/share
flows, screen viewing and hardening. Transfer resume, search/typing/mentions/activity,
registration/invite UI and tablet/accessibility validation remain.
A microphone foreground service now exists (latest ownership continuation below).
FCM/no-Google variants, optional gateway and mobile OAuth remain missing. External credentials are unconfigured. Physical audio routes/echo/calls,
screen-off battery/latency and cellular handover are unverified.

## Durable memory

Shared vault: `/home/howie/.agents/vaults/claude-obsidian`.
Read `wiki/hot.md`, then the index/relevant
`wiki/sessions/OmaChat-native-voice-chat-build.md`. Android outcomes are recorded there.
Use installed vault skills and per-file locks for updates. Never save credentials.

## Physical-phone APK update — 2026-10-02

Samsung SM-S908U was reconnected. Rebuilt the current debug APK, installed with
`adb install -r` over the existing debug application and verified a successful
cold Activity launch. App data was preserved; no uninstall, data clear, test-account
login or instrumentation was performed. Physical E2E workflows remain unverified.

## Continuation — 2026-10-02

Implemented Settings encryption-device listing and confirmed key revocation, including
self-revocation without local-secret deletion or automatic republishing. The list
uses full SHA-256 key fingerprints and server registration timestamps; no device
names exist in this protocol. Revocation does not terminate login sessions. The
Messages tab now opens actual private conversations instead of an obsolete placeholder.

Room schema 5 adds event mutations/tombstones; 6 adds their revision index. Migrations
preserve installed data, including the interim emulator schema. Cache ordering fixes
prevent old idempotent receipts or in-flight history from overwriting later edits or
resurrecting deletions. Whole-cache replacement invalidates pending history/receipts.
The isolated workflow includes two new MessageCache tests; migration fixtures cover
versions 1–5 to 6 and recreate original indexes.

Samsung is attached as of this session's inventory but was not installed, cleared,
or tested. The existing omachat-test emulator was started and used explicitly.
Original full requirements were not located in repository files; the milestone docs
remain the available requirement record. No separate repository contributor file
exists; README Development and /home/howie/AGENTS.md were read.

Validation: CMake/CTest 190/190; protocol JVM 4/4; debug/unsigned R8 release; lint
0 errors/15 warnings; both APK alignment checks passed. Expanded emulator workflow
12/12 tests/phases verified in two runs: community/persistence 6 + process-death 2,
then final crypto-only 4. No final single combined rerun. UI Settings navigation,
fingerprint display and cancelled self-revocation passed; actual extra-key revocation,
foreign-key rejection and self-revocation across reconnect passed. See implementation
status for corrected fixture/schema/UI-selector failures and exact temporary logs.

Next: persisted resumable transfers and broader SAF failure coverage; historical
sender trust with explicit provenance; remote login sessions; then native voice/media
and the remaining full Android milestones. Do not interpret key revocation as session
termination. Android socket-write failure injection and deep archive reconciliation
still need work. New code was not installed on the physical phone.

## Foreground transfer continuation — 2026-10-02

Implemented application-owned stage/byte progress, cancellation, single-worker bounds,
bounded known-ticket cleanup and partial-output warnings. The composer retains text,
reply and file selection on failed/cancelled upload and clears after durable enqueue;
later draft edits are preserved. File selection still does not survive process death.
`Transfers.kt` owns progress/cancellation; `TransferTest` adds five regressions, including
real server upload cancellation and injected/provider-open export failures. No Room,
protocol or server migration in this continuation.

One complete emulator run passed **17/17 tests/phases** (11 ordinary + two process-death
+ four crypto). Debug/test/unsigned R8 release, protocol JVM 4/4, lint 0 errors/15 warnings
and both APK alignment checks passed. Prior CTest 190/190 was not rerun because no
C++/server/protocol changed. Emulator disappearance stopped the first attempt before
installation; restarting the existing AVD and the full rerun passed. A test callback
compilation error was corrected before the passing run. Samsung remained untouched.
Logs use `/tmp/omachat-transfer-*.log`; see implementation status for precise evidence.

Next: persist staged ciphertext/metadata and safe transfer resume across reconnect and
process death, broader SAF/quota/failure testing, historical sender trust and remote
login-session management, then the remaining native voice/media/mobile milestones.
Do not call transfers resumable: no upload/download journal or retry UI exists yet.
Direct Compose progress/cancel interaction tests are also still missing; the new
automated tests exercise transfer boundaries, while the UI is build-verified.
Cancellation does not interrupt an in-flight native crypto/provider syscall. Mid-write
export failure is injected at the output-stream boundary; real provider coverage is
limited to opening an unavailable content URI. Full Android requirements remain absent
from repository/vault records; the milestone docs are the available scope record.
All previous uncommitted work and session-ses_f0f5.md remain preserved. No commit,
push, hosted change, production signing or release publication.

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
production signing or publication. Next (updated by explicit user instruction): native
Android voice, followed by persisted resumable transfers and remaining full mobile
milestones. Historical unseen-sender recovery remains a documented limit.


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
