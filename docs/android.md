# Android client

The Android implementation is in `android/` on `feature/android-client`. It is
an implemented community-text vertical slice, **not a completed mobile release**.
See [implementation status](android-implementation-status.md) for evidence and gaps.
It connects directly to an OmaChat TLS server; the desktop is not a proxy.

## Build and install

Install Java 21, Android SDK command-line tools, platform-tools, stable SDK 37 and
build-tools 36.0.0, NDK 28.2.13676358 and CMake 3.31.6. Set `ANDROID_HOME` to your SDK and `JAVA_HOME` to Java 21.
No SDK paths, signing keys, or generated APKs belong in Git.

```sh
sdkmanager 'platforms;android-37.0' 'build-tools;36.0.0' 'ndk;28.2.13676358' 'cmake;3.31.6'
cd android
./gradlew :app:assembleDebug :app:assembleRelease
./gradlew :protocol:test :app:lintDebug
adb -s emulator-5554 install -r app/build/outputs/apk/debug/app-debug.apk
adb -s emulator-5554 shell am start -n org.omachat.android.debug/org.omachat.android.MainActivity
```

Minimum Android 10/API 29; compile/target 37. Gradle 9.4.1, AGP 9.2.1,
Kotlin 2.3.10, Java 17 bytecode. Dependencies and Gradle distribution checksum are
pinned in the build files. The debug identity is `org.omachat.android.debug`.
The APK includes arm64-v8a and x86_64 crypto libraries and AndroidX native dependencies.
ELF and APK 16 KiB alignment is checked with `tools/check_native_alignment.py`.
Runtime testing is on a 4 KiB API 36 x86_64 emulator; 16 KiB runtime and physical
arm64 execution of this slice remain unverified.

Enter the TLS hostname, port, and existing username/password. A self-signed server
requires explicit acceptance of its complete SHA-256 certificate fingerprint.
Confirm it through a trusted operator channel. A changed certificate stops login.
Re-enter credentials after first trust; the password is not retained across it.
This build cannot register accounts or join invites through its production UI yet.

## Emulator and isolated end-to-end check

```sh
sdkmanager emulator 'system-images;android-36;google_apis;x86_64'
echo no | avdmanager create avd -n omachat-test -k 'system-images;android-36;google_apis;x86_64' --device pixel_6
emulator -avd omachat-test -gpu swiftshader
# From the repository root, after installing documented desktop build dependencies:
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j4
ctest --test-dir build -j4 --output-on-failure
PATH="$ANDROID_HOME/platform-tools:$PATH" python3 android/tools/integration.py --serial emulator-5554
```

The script creates a temporary TLS server/database and a separate null-audio desktop
daemon, then tests real Android↔desktop messages. It removes only its temporary
fixture. It installs APKs with `adb install -r` and invokes instrumentation directly;
it preserves app installation. Instrumentation signs out its disposable account.
Use a dedicated emulator, not your daily account installation. The script includes
Room migration/isolation/secret-wrapping checks, a 620-row deep-paging test and two
separately invoked process-death phases with a real `am force-stop` between them. It never targets a hosted server.
For a physical device use `--serial SERIAL --host COMPUTER_LAN_IP --bind COMPUTER_LAN_IP
--port UNUSED_ALLOWED_PORT`; both devices must reach that port. Do not disable a firewall.

Standalone persistence tests:

```sh
cd android
./gradlew :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=org.omachat.android.RoomPersistenceTest
```

Gradle's connected test task may uninstall the tested app afterward. Use disposable
emulators. The opt-in hosted-server instrumentation class is excluded from these
commands and must not be run against arbitrary live accounts.

Build variants before running lint. Running release KSP generation concurrently
with debug lint can race generated-source discovery on this toolchain; the commands
and CI deliberately separate those invocations.

## Signing

`assembleRelease` enables R8 and resource shrinking. Without signing configuration,
`app/build/outputs/apk/release/app-release-unsigned.apk` is **unsigned**. Supply all
four environment variables to use your own signing key: `OMACHAT_KEYSTORE` (absolute
path), `OMACHAT_STORE_PASSWORD`, `OMACHAT_KEY_ALIAS`, `OMACHAT_KEY_PASSWORD`.
Keep them in protected CI secrets or your shell environment; never commit them.
No production signing or publication has been performed.

## Security and limitations

Tokens are AES-GCM wrapped with an Android Keystore key in `noBackupFilesDir`.
Independent X25519 installation keys and private drafts are also Keystore-wrapped. Backups are disabled; sensitive
screens use `FLAG_SECURE`. Room stores private message/outbox ciphertext and community messages/drafts in the
app-private database without additional encryption; community plaintext is also
visible to the server. Local sign-out clears the account's local cache and secrets;
it does not delete the server account. Sign-out erases the local device key and attempts current-key revocation. Settings provides encryption-key listing and revocation; remote login-session controls are available on servers advertising sessions.manage.v1.

INTERNET, ACCESS_NETWORK_STATE, RECORD_AUDIO, FOREGROUND_SERVICE,
FOREGROUND_SERVICE_MICROPHONE, MODIFY_AUDIO_SETTINGS and POST_NOTIFICATIONS are
declared. Voice-channel taps request microphone permission; notification permission
is optional and requested for call controls. Notification denial does not prevent
Android's foreground-service task-manager indicator. Idle connections close in the
background; an explicitly joined call keeps its microphone service/control connection.
There is no push receiver or background message-sync worker. Force-stop, screen-off
and private-network reachability can still prevent delivery.

Missing features: persisted/resumable transfers, voice route selection and physical
validation, screen-share viewing, OAuth, multiple saved accounts, push, search/typing,
registration/invite/share-intent UI and full adaptive/accessibility validation.
Private DMs/groups/text/edits/files interoperate with desktop. Unsupported servers and
unavailable/changed/revoked keys block private sends, never downgrade. Room-backed
history paging has no 500-message display cutoff. Full synchronization after
unavailable replay still discards old history.

If login fails, check the TLS port, DNS/network reachability, certificate validity,
and credentials. The server must speak protocol major 1 and TLS 1.3. Do not bypass
certificate validation. Legacy servers support community text; uncertain sends
cannot be automatically retried without the idempotency capability.

## Encrypted conversations

Select an existing private conversation or start one from the community user list.
Use **New encrypted group** for 3–10 participants. Every participant needs a
published device key. The **Verify user** dialog displays the same safety number
as desktop `omachatctl e2e safety USER`. Compare through a trusted channel before
marking verified. A directory change invalidates verification and blocks sends;
explicitly reviewing the displayed number can accept keys as unverified.

**Attach file** opens Android's document picker. **Save** exports only after full
attachment authentication. The server sees ciphertext and opaque filenames for
private attachments; it still sees participant and delivery metadata. Community
attachments remain readable by the server. Files are capped at 100 MiB on mobile;
the server can impose a lower limit. Foreground transfer progress/cancellation is available; persisted resume is pending.

Cached private messages and wrapped private drafts can be read offline after
restart. A fresh installation cannot open old messages lacking its wrap. Corrupt
stored identity material is not silently replaced. Messages authenticated by this
installation while their sender key was in a live observed directory can remain
readable after key removal. They show a local-observation date and a warning that
the identity was not necessarily verified. Unseen messages and new encrypted edits
from removed keys stay unavailable. Receipts bind the exact ciphertext, message ID,
author and channel and are Keystore-wrapped per account/server instance; only the
latest 1,024 observations are retained. Evicted receipts fail closed. Offline
cached directories cannot create receipts, and this is not proof of signing time.
Previously cached messages need a live re-fetch with a registered sender key to
gain provenance; missing recipient wraps still prevent decryption. Private key material is
excluded from backup; losing it means losing access to that device's old wraps.
Settings → **Encryption devices** refreshes your own account's registered keys.
It shows SHA-256 public-key fingerprints, registration dates and this installation's
key. Compare the complete fingerprint before confirming **Revoke key**. Revocation
stops future recipient wraps for that key; it does not sign out the other device,
erase its old history or revoke its login session. Contacts must review the changed
directory. Self-revocation blocks new private sends without silently replacing the
identity; sign-out removes the local secret. Device names are unavailable in the
current protocol. The list is a last-observed snapshot while offline.

There is no forward secrecy or independent cryptographic audit.

Run only the encrypted fixture slice, optionally against an unmodified desktop:

```sh
python3 android/tools/integration.py --serial emulator-5554 --crypto-only
python3 android/tools/integration.py --serial emulator-5554 --crypto-only --desktop-binary /usr/bin/omachatd
python3 android/tools/check_native_alignment.py android/app/build/outputs/apk/debug/app-debug.apk android/app/build/outputs/apk/release/app-release-unsigned.apk
```

See [crypto source review and evidence](android-crypto-review.md) and
[native dependency provenance](../android/third_party/README.md).

## Foreground file controls

Select a single file in the conversation. Stage and byte progress appear above the
current view; Cancel transfer stops at the next chunk/checkpoint and Dismiss removes
a terminal result. Files are limited to 100 MiB and the server's lower cap. Text,
reply and the selected file stay in the composer after a failed/cancelled upload;
the composer clears after durable message enqueue. One file transfer runs at a time.

Files authenticate completely before output opens. Saving can still fail or be
cancelled partway through a document-provider write, leaving partial output at the
selected destination. Blocking provider calls and native encryption cannot stop
mid-call. There is no automatic resume after reconnect/process death yet; file
selection and transfer state are not persisted. Use the disposable emulator workflow,
which now includes five transfer cancellation/export-failure tests.

## Remote login controls

Settings → **Login sessions** is available when the server advertises
`sessions.manage.v1`. It lists up to 100 account sessions at a time, with creation
and refresh-expiry dates, session IDs and connection state observed at refresh.
**Older sessions** replaces the page; **Refresh** returns to the newest page.
Device names and network addresses are unavailable. Confirm **Revoke session** to
invalidate that login's tokens, disconnect its connections and stop its owned voice
session. Use **Sign out and remove local data** for the current login.

Session revocation preserves encryption keys and cached messages on the other
installation. Revoke its encryption key separately under **Encryption devices**
if it should stop receiving new private-message wraps. A revoked Android login
requires authentication again; it keeps its draft/cache and encryption identity.
An offline list is a last-observed snapshot. Servers predating the capability need
an operator upgrade before these controls work; no hosted upgrade was performed.


## Voice implementation boundary

On servers advertising `voice.ownership.v1`, tap a voice channel to join, grant
microphone access, then use Mute/Unmute and Leave voice in the persistent call bar.
Another login's active voice requires the separate **Move voice here** confirmation;
Cancel leaves its lease intact. Leave is also available in the foreground notification.
Voice is relay-encrypted, not participant end-to-end encrypted: the server can access
and relay audio. Android uses the authenticated TLS socket's selected IP for UDP,
shared packet encryption and desktop-compatible mono 48 kHz/20 ms Opus.

The non-exported microphone foreground service starts from visible user action,
promotes before audio/focus allocation and returns START_NOT_STICKY. No saved lease,
automatic rejoin, boot start or notification-based microphone start exists. Audio
focus loss ends the call, including transient/duck requests; join again explicitly.
Communication mode uses platform default routing and restores the prior mode on exit.
Explicit route selection, Bluetooth/SCO policy and noisy-route handling remain gaps.

Ownership loss/eviction, control loss, default-network change, sign-out, service loss,
media expiry/failure and audio failure release capture/playback/media/focus. Idle
background control closes after Leave. Network change ends voice; it does not claim
seamless handover. Moderation mute/deaf flags suppress local send/playout as well as
server relay enforcement. Deafen controls and a participant/speaking UI remain gaps.

`VoiceOwnershipTest` exercises actual application/service joins, transfer conflict,
Compose confirmation/cancel/mute controls, former-owner shutdown, background control,
notification Leave, network/sign-out teardown, queued-start cancellation and an
injected focus-loss callback/service destruction. Synthetic Opus/relay/device tests
remain in `VoiceMediaTest`/`VoiceAudioTest`. Emulator audio can be silent: none of this
establishes audible two-device desktop interoperability or physical echo/routes/calls,
screen-off battery/latency, cellular handover or runtime 16 KiB behavior. Permission
denial/revocation and notification-denial UI need dedicated validation.

The complete workflow runs transport/audio against a second fresh disposable server
and application ownership against a third, preserving registration/login rate limits. Focused validation:

```sh
python3 android/tools/integration.py --serial emulator-5554 --test-class org.omachat.android.VoiceOwnershipTest
```

Lifecycle references checked 2026-10-02: Android's [visible-start restrictions](https://developer.android.com/develop/background-work/services/fgs/restrictions-bg-start),
[audio focus contract](https://developer.android.com/media/optimize/audio-focus) and
[foreground-service task manager](https://source.android.com/docs/core/display/task-manager).
These platform contracts do not substitute for the missing physical-device tests.
