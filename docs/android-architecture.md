# Android architecture

`android/app` owns Compose UI, ViewModel/StateFlow presentation, lifecycle, networking
coordination, Room, DataStore and Keystore storage. `android/protocol` generates
protobuf-lite from the repository's authoritative `protocol/network.proto` and
implements bounded length-prefixed TLS control framing. No Flux code is copied:
its Android organization was reviewed, but it has no selected license.

The application owns one `SessionCoordinator`, retained across Activity recreation.
Process lifecycle starts foreground connectivity and closes idle background sockets.
Active voice keeps its foreground service and existing control socket while backgrounded.
Network changes end voice before serialized teardown/restart under a lifecycle mutex; sockets close
before cancellation. Refresh rotation runs within that same single account loop.
A lost refresh response requires reauthentication rather than replaying a token.
There is currently one saved account, not the required final multi-account system.

Room keys every row by endpoint/account scope plus entity ID. A new server instance
UUID is bound to the stored snapshot when advertised. A changed instance or account
ID invalidates cache/outbox before any send. Legacy servers cannot provide that UUID;
endpoint and authenticated account checks remain available. Secrets are stored
separately in AES-GCM-wrapped atomic files and excluded from backups.

Message UUID and exact protobuf payload persist before transmission. A send moves
Queued→Sending→Confirmed (removal from outbox), or Uncertain/Failed. Interrupted
Sending operations become Uncertain. Explicit retry on capable servers reuses the
same bytes and operation. Legacy ambiguous sends are not replayed. Drafts persist
separately. Account logout clears its local entities and wrapped credentials.

Reconnect resumes from the last persisted event sequence. A complete replay of at
most 32 events is consumed and applied to the cached snapshot/history; the small
batch leaves room for concurrent live events in the bounded transport queue.
Structural changes can force full reconciliation and advance beyond remaining replay
entries. Longer or unavailable replays use full sync, discard old cached history,
and fetch selected-channel pages separately. Structural snapshots alone cannot
reconcile historical edits/deletions or lost access. Preserving a deep offline
archive after unavailable replay remains work. Event application and the snapshot
replay cursor share a Room transaction.

Room PagingSource and Compose Paging render history with 50-row pages, indexed by
account/channel/timestamp/ID, a 300-item page-cache target and unloaded placeholders.
Older pages can be downloaded beyond 500 messages; UI uses stable message keys and
the DAO's actual oldest cached message for the next server cursor. All message rows
remain account scoped; the recent-message convenience query used by tests still
returns at most 500 rows.

Room schema 6 adds durable per-message mutation revisions and deletion tombstones.
History requests capture a revision fence before transmission; their replies cannot
overwrite a later event or any tombstone. Cache replacement invalidates in-flight
pages and send receipts. Idempotent acknowledgements confirm the outbox but only
insert missing, untouched messages, preserving later edits. Event mutations and the
replay cursor commit in one Room transaction. Full sync clears these records along
with history; this does not provide deep archive reconciliation.

The TLS reader never blocks behind its 64-event queue: overflow ends the connection
and forces full recovery. During full sync, pre-reply events are represented by the
snapshot; the sync reply is the wire-order barrier enabling subsequent events.
At most 32 requests are pending, each with a 20-second timeout. Frame lengths are
validated before allocation against the existing 8 MiB protocol limit.

The desktop's Qt adapters and Android JNI target now share
`shared/src/omachat/crypto/MessageCrypto.*` and `FileCrypto.*`. These bounded
operations depend only on C++ and libsodium, with no Qt, protobuf, sockets,
keyring, PipeWire, desktop IPC or Android lifecycle dependencies. Kotlin assembles
shared protobuf envelopes and owns the directory, trust, persistence and transfers.
Android links a signature-verified, hash-pinned libsodium source snapshot using
NDK r28c for arm64-v8a and x86_64; libc++ and sodium are static inside the crypto
library. Existing AndroidX dependency libraries are also present in the APK.

Each endpoint/server-instance/authenticated-account binding has an independently
generated X25519 installation key. Android Keystore AES-GCM wraps its secret;
it is not a hardware-held X25519 key and no desktop private key is copied.
Previously published identities are checked against the directory on reconnect;
a removed key is not automatically republished. Sign-out attempts revocation
before local teardown. Settings lists this account's live encryption keys with SHA-256
fingerprints, registration times and this-installation identification, and offers
confirmed key revocation. Revocation does not revoke login sessions or erase old
wraps. This installation retains its secret after self-revocation until sign-out. Directory changes invalidate verification, persist a
warning and block new encrypted operations until explicitly reviewed. Pending
ciphertext cannot be re-encrypted under the same send UUID; mismatched recipient
keys block its retry and require history inspection and a fresh composition.

Room stores private ciphertext and encrypted outbox protobufs, not private bodies.
Private drafts are separately Keystore-wrapped. Offline restoration loads the
installation key and last-known directory before a socket exists. MessageCache
records successfully authenticated private payloads from live-observed registered
sender keys before storing accepted events/history/receipts. A bounded 1,024-entry
Keystore-wrapped journal holds SHA-256 digests of the exact encrypted bytes plus
message ID/channel/author and local observation times, scoped to endpoint, server
instance and account. Cache revision fences also gate this observation callback.
Receipt writes run on I/O; display reads use memory and never write secrets.
Unknown/removed sender keys expose only bytes matching a retained observation,
with explicit provenance in the UI. New ciphertext/edits and changed IDs/context
cannot borrow old receipts. Offline directories cannot mint new receipts. The
journal survives cache replacement, but cannot reconstruct messages it no longer
holds or prove a historical directory or signing time. Sign-out removes receipts;
eviction/corruption fails closed without replacing the installation identity. New installations cannot decrypt
messages that lack their device wrap. No history key copying or forward secrecy
is claimed. Verification is trust on first use plus safety-number comparison;
there is no audited key transparency service.

Files use the desktop secretstream header and 64 KiB framing. SAF import/export
uses bounded app-private staging; message metadata seals the real filename, MIME,
size and key. Downloads verify digest, final tag and sealed size before writing
to the document provider. Interrupted staging files are removed on process start.
One attachment can be selected per composition; mobile files are capped at 100 MiB
(and the server's lower limit). Application-owned stage/byte progress and cancellation
allow one foreground transfer at a time. Known upload tickets get bounded cancellation
cleanup; import/export loops check cancellation between 64 KiB chunks. The composer
clears only after durable enqueue and preserves later edits. Sign-out cancels/joins the
active transfer before removing secrets. Native crypto and blocking provider calls
cannot be interrupted mid-call. Failed/cancelled export can leave partial provider
output and the UI warns accordingly. Transfer resume, persisted upload operations,
recovery across reconnect/process death and provider write atomicity remain work.

Android now has an internal native packet/UDP/jitter transport foundation (see below);
an internal Opus/AudioRecord/AudioTrack pipeline is now implemented (see below),
now connected to explicit production joins and a microphone foreground service (see latest section below). Crypto source review, exact bounds,
validation and limitations are recorded in [crypto review](android-crypto-review.md).

Remote login-session controls use `sessions.manage.v1` and a bounded 100-row page.
Refresh/revoke actions serialize with lifecycle changes, require a connected socket,
and verify reply shape/current-session identity before replacing the observed list.
Lists remain memory-only; login credentials are never included. Android uses local
sign-out for current-session removal. A remotely revoked coordinator reconnects,
fails resume/refresh and enters LoginRequired without erasing its account cache,
drafts or independent encryption identity. The server persists account-scoped
session deletion, invalidates every access grant and closes all matching sockets;
voice teardown matches owner session and leaves other logins intact.


## Native voice transport foundation (2026-10-02)

`VoiceMedia.kt` and `MediaJni.cpp` reuse the desktop's Qt-free `MediaPacket.cpp`
inside the existing sodium JNI library. There is no new dependency or wire format.
Packets are capped at 1,400 bytes, with 1,360 payload bytes; JNI validates lengths,
header/version/type/reserved bytes, key and direction before authentication.
Native key copies are wiped. Media is relay-encrypted, not participant end-to-end
encrypted; the server decrypts and re-seals for each recipient, as on desktop.

One connected UDP socket pins reception to its selected peer. Registration retries
at 500 ms until an authenticated, own-stream RegisterAck, with a 10-second monotonic
deadline, then 15-second keepalives. Expiry and socket failure end the transport.
The owner supplies the control socket's authenticated peer address; production
control/lifecycle integration is still pending. No automatic transfer/rejoin occurs.
Opening cancellation closes the socket; stop unblocks reception, joins workers and
wipes the local media key. Lease protobuf/runtime copies cannot be guaranteed wiped.
Media keys are never written to disk.

Counters are serialized, consumed before send and reject 32-bit exhaustion.
A process-local, non-evicting set of at most 1,024 key/stream digests rejects lease
reopening, including after failed registration; exhausting it requires app restart.
After restart a fresh control join is required; leases must never be persisted.
Authentication precedes bounded replay admission (128 stream/type windows), and
64-frame windows reject duplicates and old sequences without wrapping.
There are at most 64 speaker queues of 64 frames each per lease. These lifetime
bounds fail closed for new streams after saturation; stream-churn reconciliation
with live voice state remains work. A fixed 60 ms startup jitter target, backlog
skip to three frames above six queued frames and 240 ms stall reset bound delay.
Playout is intended for a 20 ms caller clock; missing ticks return no frame.
Adaptive jitter, FEC/PLC decoder scheduling and actual mixing remain unimplemented.

This internal foundation is instrumentation-tested, not reachable from the current
voice-channel UI. Opus capture/decode, Android AudioRecord/AudioTrack, foreground
microphone service, permission/focus/route/interruption handling and application
ownership-loss teardown are the next required work. Transfer ownership is tested
against the real server, but tests explicitly stop the former local transport.


## Internal native audio pipeline (2026-10-02)

`OpusJni.cpp` links desktop's unchanged Qt-free `OpusCodec.cpp`, using hash-pinned
upstream Opus 1.6.1. JNI exposes monotonically allocated IDs rather than pointers;
serialized lookup/use/destruction and a 256-handle process bound protect stale IDs
and concurrent close. Capture accepts exactly 960 finite normalized float samples;
decode accepts one 20 ms frame at 48 kHz or explicit null for PLC, capped at 1,275
bytes. Other durations are rejected before changing decoder state. The Kotlin
`VoiceCodec` serializes encode/decode/close and owns one encoder or decoder.

`VoiceMedia.playoutTicks` distinguishes active lost speech from startup/idle, so
`VoiceMixer` conceals only active ticks, with one decoder per stream, at most 64.
Inactive decoders are destroyed; malformed packets produce one PLC tick. Mixing
sums/clamps 960-sample mono frames. Fixed jitter remains; no adaptive jitter or
lookahead FEC scheduling is implemented. Transport speaker/replay lifetime bounds
and missing live-participant reconciliation remain unchanged.

`AndroidVoiceDevice` checks microphone permission before allocating float mono
48 kHz AudioRecord with VOICE_COMMUNICATION and AudioTrack with communication
attributes. Platform AEC is enabled when available, without claiming echo quality.
The internal `VoiceAudio` worker waits for relay registration, handles partial
nonblocking reads/writes, rejects stalled input/output and negative device results,
and increments timestamps without wrapping. Playback follows a 20 ms monotonic
clock and skips late scheduling bursts. A mute flag suppresses transmission.
Explicit stop cancels/joins before device release. Atomic coroutine entry ensures
immediate stop still releases a supplied device. Failure closes media and codecs;
no capture samples, codecs or media leases are persisted.

This layer is not launched by the application or reachable through UI. The next
layer must start a microphone foreground service from explicit visible user action,
request runtime permission, bind to the authenticated control peer, manage audio
focus/routes and stop on ownership loss, control disconnect, sign-out and network
changes. Current application background sockets still close. Physical sound/echo,
Bluetooth/headsets, calls, screen-off, battery/latency and handover are unverified.


## Application voice ownership and foreground service (2026-10-02)

SessionCoordinator stages a process-only generation token from visible UI action.
VoiceService intents carry that token alone; no account credentials or media keys.
The non-exported service promotes on every foreground-start command, including
cancelled/stale commands, then rejects invalid tokens and stops. It never restarts
sticky or from boot. Join jobs are cancellable separately from the control loop;
Leave/network/sign-out revoke staged tokens before suspended lifecycle work.

VoiceCall serializes resource ownership, uses the authenticated TLS peer IP for UDP,
awaits registration, observes current user/session/channel/stream through an ordinary
Sync call and acquires service audio focus before opening Android devices. That Sync
does not discard queued coordinator events. Its sequence fence prevents already
represented old leave/ownership events from tearing down the new lease. Subsequent
self events and structural sync reconcile moderation and ownership. Generation-scoped
failure callbacks cannot end a later call. Media/audio/focus and the service release
on ownership loss, control loss, network change, sign-out, expiry/failure and Leave.
An ambiguous join without a confirmed lease closes its control owner; no automatic
transfer/rejoin occurs. Known leave cleanup is bounded; server disconnect cleanup is
still relevant when replies are unavailable. Audio keys/leases remain memory-only.

Microphone permission precedes foreground start; notification permission is optional.
Audio focus loss ends voice; communication mode uses platform default routes and
restores previous mode on service cleanup. Flags are installed before audio workers
start; local mute/deaf honor self and moderation state. Explicit routes/SCO/noisy-route
policy, deafen/participant UI, adaptive jitter/FEC/live-stream reconciliation and
physical sound/echo/calls/screen-off/handover remain incomplete. Existing earlier
internal-layer descriptions record their implementation stages, not current UI state.
