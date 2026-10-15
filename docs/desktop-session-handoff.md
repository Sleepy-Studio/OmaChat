# OmaChat desktop session handoff

Updated: 2026-10-15. Resume desktop development from this file.


## Phase 2B installed and verified — 2026-10-15, 08:48 UTC

Favorites, hiding, persistent category collapse, compact channel rows and optional
voice participants are implemented with device-local account scope. Transactional
local **schema 4** preserves notification settings and other account data. Older
daemons cannot open schema 4. `sidebar.get/set`, snapshots and account-tagged events
serve the GUI and CLI; write failures do not publish successful state.

Favorites appear once in a deterministic section and have keyboard move actions.
Reordering keeps keyboard focus on channel identity, includes hidden selected and
active-voice favorites, and leaves server placement unchanged. Hidden channels
remain discoverable in the quick switcher and **Sidebar — On this device** /
**Hidden channels** management. Selected channels and active voice survive hiding
or collapse; notification eligibility is unchanged. Permission resync removes
channel/artwork presentation while retaining preferences. Confirmed channel
removal clears preferences for active/background accounts.

Both final main/debug **0.2.2-1** packages are installed. All **129 installed regular
files/symlinks** match extracted packages; `pacman -Qkk omachat omachat-debug`
reports zero alterations. Running daemon/client binaries match, the relaunched
client is visible, and both saved accounts reconnect without new login. Live schema
4 integrity passes and preserves both accounts/pins, 14 known-key rows, one volume
row and the existing notification-preference row. Fresh private schema-3 backup:
`~/.local/share/omachat/backups/omachat-before-phase2-sidebar-install-20261015T084740Z.db`
(0600; integrity checked).

Installed CLI acceptance on existing disposable channel `236109384625688576`
passes favorite → hide → new-connection read → unhide → remove favorite, plus both
view toggles. Original preferences are restored exactly, including removal of the
default rows created only for acceptance. No hosted content changes are needed.
Voice is restored to its previous channel with microphone muted; sharing is off.
These are dated live observations; recheck before later disruptive work.

Validation: **469/469 final package CTest checks** (459 non-voice with real keyring
enabled and CI's platform theme/fonts, then 10 serial voice checks with compilation
idle); **3/3 package-built native Wayland checks**, including the settings dialog's
dark/light 720×460/150% and 1440×900/100% keyboard/pointer matrix, favorite keyboard
focus and notification-dialog regression. Four settings captures are inspected.
The additional favorite capture changes native window size after closing the menu;
it demonstrates focus/order, not a final 720×460 capture. The extracted production
executable also renders on Wayland with isolated paths and no daemon connection.
QML lint, changed C++ formatting, whitespace and desktop-entry validation pass;
existing QML type warnings and generated-Protobuf LTO warnings remain. Independent
source review approves the final changes.

Phase 2A exact-SHA desktop CI at `daf7830` fails only the notification Close test's
platform-dependent Enter activation; Android passes. The failure reproduces with
CI's empty platform theme. The test now uses portable Space activation and passes
three repeats in each targeted configuration. An independent review also catches
and resolves hidden selected/voice favorite movement. Superseded package runs
expose a stale harness object: copying refreshed files with old mtimes lets Ninja
skip a later-built test controller. Force-refreshed source mtimes rebuild every
affected target; the complete final package suite then passes. Failed/superseded
logs and artifacts are retained; only packages directly under `install/` are final.
An initial live restore helper uses the wrong CLI spelling (`voice mute`); it is
corrected to `mute` before rejoining, with the microphone muted throughout the
restored call.

Evidence: `build/desktop-phase2-sidebar/checkpoint.json`, `evidence/`,
`native-captures/`, and `install/source-manifest.json` (451-file snapshot). Package
application source matches this checkpoint; subsequent changes are documentation
only. Commit/push and fresh exact-SHA CI outcomes are recorded in local evidence
and shared memory. No main merge, hosted rollout, public release/AUR or vault Git
commit is included.

**Next:** check exact-SHA CI after push, then implement **2C unread recovery and
controls** from the Phase 2 plan. 2D mention kinds, human usability, physical Android
and MLS/SFrame gates remain separate. The checkpoints below are historical.

## Phase 2A installed and verified — 2026-10-15, 05:36 UTC

The user authorizes installation during active voice, followed by commit and push.
Both final main/debug **0.2.2-1** packages are installed. All **128 installed
regular files/symlinks** match extracted artifacts, and `pacman -Qkk omachat
omachat-debug` reports zero altered files. Both running binaries match installed
files; the relaunched native client is mapped and visible. Both saved accounts
reconnect without new login. Voice is restored to the prior channel with the
microphone muted; screen sharing remains off.

The live database is now **schema 3**. Comparison with the fresh private online
schema-2 backup preserves both accounts and pinned trust, all 14 known-key rows,
and the one saved volume row. This live database had zero legacy mutes; migration
of indefinite mutes is covered by the independent schema-1/2 fixtures. Backup:
`~/.local/share/omachat/backups/omachat-before-phase2-install-20261015T053511Z.db`
(integrity checked, private permissions).

Installed daemon/CLI acceptance on the existing disposable channel
`236109384625688576` (`wyatt-reframe-test-20261014`) passes: set All, mute for 1h,
read persisted preferences through a new IPC connection, unmute while retaining
All, and restore the original preferences exactly. No hosted channel/content
changes are made. This is live IPC/CLI acceptance; the dark/light compact/wide
native GUI matrix remains automated isolated-fixture evidence.

The prior **461/461 package checks**, **2/2 native Wayland checks**, lint/format
and source/artifact hashes remain applicable: only documentation changes follow
the tested package snapshot. Independent pre-commit source review passes with no
security or logic findings. Existing keyring recovery is included in this source
checkpoint and installed package. Installation/runtime/migration/live evidence is
in `build/desktop-phase2-notifications/evidence/installation-checkpoint.json`,
`install.log`, `install-preflight.json`, `live-preferences.json` and
`installed-live-status.json`; `checkpoint.json` marks the package installed.

This checkpoint supersedes the pending installation instructions below. Commit
and push target the current `feature/desktop-accessibility` branch; exact Git and
CI outcomes are recorded in local evidence/shared memory. No main merge, hosted
server rollout, public release/AUR or vault Git commit is part of this operation.

**Next:** check exact-commit CI after push, then implement 2B favorites/hiding/
collapse from the Phase 2 plan. 2C unread recovery and 2D mention kinds remain
separate. Human usability, physical Android and MLS/SFrame gates remain open.

## Phase 2A ready for installation — 2026-10-15

Device-local, account-scoped notification levels (`default`, `all`, `mentions`,
`none`) and mute overlays (1h, 8h, 24h, until changed) are implemented and packaged.
Transactional local schema **3** preserves legacy mutes as indefinite overlays,
rolls back failed migrations, and cascades account removal. Older daemons cannot
open schema 3. A private online schema-2 backup passes integrity checking:
`~/.local/share/omachat/backups/omachat-before-schema3-20261015T043028Z.db`
(directory 0700, file 0600). Refresh it if further live changes need preserving.

One pure policy drives active/background delivery, with direct expiry evaluation
and a next-expiry timer plus bounded clock/resume rechecks. Global DM/community
switches and self/DND/focus/access suppressors apply; expiry never replays messages.
Checked get/set IPC rejects stale account writes and invalid fields, reports
storage failures, and broadcasts only persisted changes. Preference events carry
account identity. Legacy mute events project the active account only, protecting
old GUIs from background events with colliding channel IDs.

The channel menu opens **Notifications — On this device**, with level, duration,
local expiry, Unmute, pending/error state and keyboard scrolling at compact scale.
Snapshots/events reject foreign or stale account state. Offline GUI startup also
reloads its cached snapshot when the account identity arrives. CLI retains
mute/unmute and adds `channel mute CHANNEL --for 1h|8h|24h|forever` and
`channel notifications CHANNEL default|all|mentions|none`, with JSON results.

Validation: **461/461 package CTest checks pass** (451 non-voice with both real
keyring checks enabled, then 10 serial voice checks with compilation idle).
**2/2 package-built native Wayland checks pass**, including 720×460/150% and
1440×900/100%, dark/light, keyboard/pointer and colliding-account isolation.
Four native captures are inspected. QML lint passes with existing type warnings;
changed C++ formatting, whitespace and the extracted desktop entry pass. The
extracted production executable launches on Wayland with isolated paths; its
cold-start capture is inspected (no daemon connection is claimed for that smoke).

The clean Release build is followed by two incremental package refreshes. The
first tightens legacy event compatibility; full testing then finds a new offline
GUI bootstrap regression (450/451 pass). Its unchanged existing test reproduces
red; a single account-change refresh condition fixes it, and the final complete
suite passes. Initial failed logs and superseded packages remain available. GCC
LTO emits the previously observed generated-Protobuf swap warnings outside the
changed application code; all builds succeed.

Final main/debug **0.2.2-1** packages, extracted files, 450-file source manifest,
SHA-256 metadata, logs and captures live in `build/desktop-phase2-notifications/`.
Only the final packages directly under `install/` are current; do not install the
`initial-superseded/` or `compatibility-superseded/` artifacts. Keyring source edits
are preserved. Local HEAD is `87822ff`; all new changes remain uncommitted/unpushed.
No remote CI, hosted deployment, public release/AUR or vault Git commit is claimed.

**Installation is pending an interruption choice.** At 04:40:46 UTC both saved
accounts are connected; voice is joined and muted, with no screen sharing. The
upgrade hook restarts the daemon, so the standing active-voice timing gate applies.
No installed application or live database schema has changed in this continuation.

Resume:

1. Recheck `omachatctl status --json`, package/source hashes in
   `evidence/packages.json`, and the database backup. Install when idle or after
   explicit authorization to interrupt the active voice session.
2. From the repository root, install both final packages:
   `sudo pacman -U build/desktop-phase2-notifications/install/omachat-0.2.2-1-x86_64.pkg.tar.zst build/desktop-phase2-notifications/install/omachat-debug-0.2.2-1-x86_64.pkg.tar.zst`.
3. Verify all installed regular files/symlinks against `install/extracted-main`
   and `install/extracted-debug`, run `pacman -Qkk omachat omachat-debug`, verify
   running daemon/client binary parity, relaunch the client if needed, confirm
   both saved accounts reconnect and local schema 3 is active. Record live
   notification-preference acceptance separately from automated fixture results.
4. Update both handoffs and shared memory with installation/runtime outcomes.
   Then proceed to 2B favorites/hiding/collapse. 2C unread recovery and 2D mention
   kinds remain separate; human usability, physical Android and MLS/SFrame gates
   are still open.

## Phase 2 plan completed — 2026-10-14

Continued the next planning step after installed/live artwork acceptance. The
source-backed implementation plan is [Phase 2 personal preferences](phase2-personal-preferences-plan.md).
Use device-local, account-scoped preferences first, with explicit UI copy.
Implement **2A notification levels and timed mute** before favorites/hiding (2B),
unread recovery/controls (2C), and protocol-backed mention kinds (2D).

Source inspection confirms local schema 2, boolean mute storage, separate
active/background mute caches, in-memory desktop unread/collapse state, existing
server read markers, and user-ID-only mention metadata. The plan specifies a
transactional migration preserving indefinite mutes, account-tagged IPC with
legacy projections, expiry policy, write-failure behavior and acceptance gates.
Read markers remain server-backed; role/everyone controls are a separate protocol
slice. These are design choices and source findings, not shipped Phase 2 behavior.

**Next:** implement 2A storage migration and pure notification policy tests, then
daemon/IPC, CLI and GUI. Preserve the installed uncommitted keyring changes and
ignored evidence. This continuation changes documentation only; no application
rebuild, install, runtime interruption, commit/push or deployment was needed.
Prior package/native evidence remains historical. Recheck live activity before
future disruptive work. Human usability, physical Android and MLS/SFrame remain open.

## Keyring fix installed; live artwork acceptance passed — 2026-10-14

The user explicitly authorized immediate installation despite active voice/sharing,
and authorized creating a test channel. Both prepared Arch main/debug 0.2.2-1
packages are installed. All **127 installed artifacts** match extracted packages;
`pacman -Qkk omachat omachat-debug` reports zero altered files. Both running
binaries match installed files, the client is mapped/visible, and both saved
accounts reconnect without new login. Voice rejoined; screen sharing stopped
through the restart and was not automatically restarted. These are volatile
observations; recheck before further disruptive work.

Created **wyatt-reframe-test-20261014** in Sleepy Studio, channel
`236109384625688576`. A native Wayland harness uses the production packaged
ChannelSettingsDialog/ArtworkCropDialog and AppController against the installed
daemon and live hosted endpoint. Dark/light, 720×460 at 150% and 1440×900 at 100%
checks pass. Keyboard Appearance/Reframe actions restore the retained 256×128
source and right-half crop. Uploading a left-half crop selects pixels excluded
from the original blue icon; the resulting full and small variants are red,
512×512 and 128×128. Source download preserves both red/blue halves and immutable
source-pixel framing is `{x:0,y:0,width:128,height:128}`. The test channel is left
available for inspection; existing channels/artwork were not edited.

A temporary ordinary member can download FULL/SMALL but SOURCE returns the
intentional privacy-preserving `NotFound`. The successful test account is deleted.
Two initial harness mistakes are recorded: the first UI run did not open the
Appearance section; the first permission assertion expected a different denial
code, and its cleanup omitted the account id. That first temporary session was
lost before self-deletion. The orphan test account `wyatt-test-d366eb23`
(`236110401350144000`) was removed from Sleepy Studio and suspended, revoking
sessions. **Its disabled server account row remains**; no hosted database deletion
is claimed. No test credentials are retained in evidence or shared memory.

Evidence: `build/desktop-keyring-recovery/evidence/installation-checkpoint.json`,
`install.log`, `live-ui.log`, `live-artwork-result.json`,
`initial-test-account-cleanup.json`, `installed-live-status.json` and
`build/desktop-keyring-recovery/live-captures/`. Earlier full 444/444 package
checks and the separately repeated 2/2 real-keyring checks remain test evidence.
The first UI harness failure is preserved separately.

The immediate installation/live artwork gates are complete. The keyring source
fix remains uncommitted/unpushed; no hosted application deployment, release/AUR,
or vault Git commit occurred. Next implementation scope is Phase 2 personal
organization/notifications; human usability, physical Android and MLS/SFrame
acceptance remain distinct open work. This checkpoint supersedes the pending
installation/test-channel instructions below.

## Current resume point — 2026-10-14, 12:51 UTC

The remaining immediate handoff work is **local installation and live acceptance**.
The keyring recovery application source matches the prepared, tested package;
only the two handoff documents differ from its source manifest. Package and
manifest SHA-256 hashes have been rechecked. Both opt-in real-keyring tests pass
again (2/2); the earlier complete 444/444 package result remains the full-suite
evidence. Formatting and whitespace checks pass.

Both saved accounts are connected and the endpoint advertises
`artwork.framing.v1`. Voice and screen sharing are **active** at this observation.
The upgrade hook restarts the daemon, so installation awaits either explicit
interruption approval or a fresh idle observation. No restart was performed.
A designated disposable server/channel is also pending for live retained-source
upload and Reframe acceptance. Do not mark these two steps complete from the
package tests. Evidence: `build/desktop-keyring-recovery/evidence/handoff-*`.

Resume in this order:

1. Check `omachatctl status --json` immediately before installation. Respect the
   pending interruption choice if voice/sharing remains active.
2. Install both prepared packages with
   `sudo pacman -U build/desktop-keyring-recovery/install/omachat-0.2.2-1-x86_64.pkg.tar.zst build/desktop-keyring-recovery/install/omachat-debug-0.2.2-1-x86_64.pkg.tar.zst`.
   Compare installed regular files/symlinks to `install/extracted-main` and
   `install/extracted-debug` (under `build/desktop-keyring-recovery/`); run
   `pacman -Qkk omachat omachat-debug`. Verify running daemon/client binaries
   match the installed files, relaunch the client if needed, and confirm both
   saved accounts reconnect without new login. Keep account data intact.
3. In the designated disposable target, exercise opt-in full-image retention,
   upload, reopen Reframe, select pixels outside the first crop and explicitly
   save. Verify persisted crop/source dimensions and management-only SOURCE
   access. Record native/live evidence separately from automated checks.
4. Record installed/runtime parity and live acceptance in both handoffs and the
   shared vault. The new keyring fix is still uncommitted/unpushed; remote CI
   at `87822ff` validates the earlier CI repair. No hosted deployment is needed
   for this client/daemon-only fix. No public release/AUR or vault Git commit.

Phase 2 organization/notifications is proposal work after this checkpoint;
human usability, physical Android and MLS/SFrame acceptance remain separate.
The sections below are historical checkpoints; this resume point takes precedence.

## Stale keyring session recovery — 2026-10-14

The exact-SHA CI repair at `87822ff639e6f274c9edaa5f8115ae3d201743f8`
is now confirmed green: [desktop CI](https://github.com/Sleepy-Studio/OmaChat/actions/runs/38545670054)
passes format/plugin, 202 unit/fuzz, 225 integration and GUI smoke checks;
[Android](https://github.com/Sleepy-Studio/OmaChat/actions/runs/38545669926)
passes build/lint/protocol and emulator persistence jobs. This is evidence for
that CI-only commit, not remote validation of the new local keyring changes.
Remote main remains `2b33fb9`; desktop branch remains `87822ff`.
Completed remote logs/status are retained in `build/desktop-ci-permissions/evidence/`.

The stale-session error is now faithfully reproduced. The original probe closed
an independent `SecretService` session rather than the password backend's
cached session. Closing the probe process's own `SecretBackend` session produces
`The session does not exist` on repeated reads and `The session wrapping the
secret does not exist` on writes. The public `secret_service_disconnect()` clears
both caches in the installed libsecret 0.21.7 and restores access. No keyring
service or application restart is needed for this isolated recovery.
Upstream provenance: [libsecret 0.21.7 service/cache implementation](https://raw.githubusercontent.com/GNOME/libsecret/0.21.7/libsecret/secret-service.c).

`CredentialStore.cpp` now resets the process-local libsecret proxy and retries
reads/writes once on QtKeychain `OtherError`, which includes expired sessions.
Missing-entry and access-denied handling is unchanged. A repeated error is
reported after the retry; missing reset support reports the original error.
Writes repeat the same key/value. Production resolves only the public reset
function dynamically; the unstable backend/session APIs are confined to tests.
Removal needs no secret-transfer session and retains its original behavior.

The opt-in real-keyring regression first fails on the exact stale read, passes
after read recovery, then fails on the expired-session write and passes after
write recovery. It verifies the raw QtKeychain failure, recovered secret, updated
value and synthetic-entry cleanup. Both this regression and the existing encrypted
send-recovery real-keyring round trip pass. Tests run with
`OMACHAT_TEST_REAL_KEYRING=1`; ordinary CI skips these desktop-keyring checks.
Changed C++ files pass their actual compiler flags with `-Werror`; formatting and
whitespace checks pass. All **418/418 non-voice unit/fuzz/integration checks**
pass locally offscreen with real-keyring checks enabled. An earlier broad run was
stopped before completion to set the offscreen platform and correct the voice
exclusion; its partial log is preserved and is not counted as a full pass.

The clean Release main/debug package builds successfully and passes **444/444
checks**: 434 non-voice checks with both real-keyring tests enabled, followed by
10 serial voice checks with compilation idle. This includes all headless GUI
checks. Remote CI for the new uncommitted keyring fix has not been run.

Main/debug Arch **0.2.2-1** packages are prepared from the **444-file** local
uncommitted snapshot based on `87822ff`. Source manifest SHA-256:
`110aebe880bf670cf50bd97595bb852d583b19cc57ae032623c30c52c2afb5be`.
Packages are extracted for future install verification. Release/LTO emits GCC
`-Wstringop-overflow` warnings in generated Protobuf swaps outside the changed
credential files; the build succeeds. No new native GUI acceptance is claimed.
Evidence: `build/desktop-keyring-recovery/` and
`build/desktop-ci-permissions/evidence/credential-*`.

The prepared packages are **not installed**. Fresh live status remains connected
with `artwork.framing.v1`, voice joined and screen sharing active. The package
upgrade hook restarts per-user daemons, so installation would interrupt this
session. An explicit install-now versus wait-until-idle question is pending.
The running application remains the previous installed snapshot.

**Resume:** install the prepared packages when an application interruption is
acceptable, then verify installed/runtime artifacts and connected account recovery.
Live retained-source/Reframe acceptance still awaits a designated disposable
server/channel (requested this continuation). Phase 2 organization/notifications,
human acceptance, Android physical-device and MLS/SFrame gates remain separate.
No commit, push, hosted deployment, public release/AUR or vault Git commit is
performed during this continuation. Prior edits and ignored evidence are preserved.


## CI permission regression repaired — 2026-10-14

The four failed assertions reproduce exactly when the artwork enumeration test
runs as root. CI's Arch container runs its unit/integration steps as root;
CAP_DAC_OVERRIDE allows enumeration of the chmod-000 directory. The same test
passes as the ordinary desktop user. No production cache change is required.

`.github/workflows/ci.yml` now creates the existing GUI test user immediately
after building and runs unit/fuzz, integration and GUI checks under that account.
The permission regression remains unchanged and is exercised rather than skipped.
Current test targets rebuild successfully; all **202 unit/fuzz checks** pass
(the opt-in real-keyring round trip passes in a separate enabled run), and all 36 ArtworkCache-matched
checks pass through a root-to-user `su` invocation. Actionlint and whitespace pass.
Evidence: `build/desktop-ci-permissions/evidence/`.

CI/documentation-only repair **c3626222906f44c1fdd1ad57faed4972e754b9a9** is
pushed to the desktop branch. Exact-SHA CI run `38541795394` passes build,
format/plugin and all 202 unit/fuzz checks; Android run `38541795417` also passes.
Integration reaches 225 checks: 222 pass and three GUI layout checks fail.
The GUI smoke step is not reached. This is not a fully green CI checkpoint.

A Fontconfig configuration with no fonts reproduces all three failures locally,
including the exact permission-dialog Close position (914 versus 720 pixels).
An isolated Liberation-only font configuration passes all three unchanged checks.
The CI dependency list now explicitly installs `ttf-liberation`; no application
layout or test assertion is changed. Font-fixture logs and failed CI logs are
retained in the same evidence directory. Font repair **87822ff639e6f274c9edaa5f8115ae3d201743f8** is pushed to the desktop
branch. Exact-SHA CI run `38545670054` and Android run `38545669926` are in progress;
format/plugin checks pass. Main remains at2b33fb9; no automatic deployment occurs. The entire local
**225/225 integration suite** passes with only Liberation fonts available
(`liberation-integration.log`), alongside 202 unit/fuzz checks and the separately
enabled real-keyring round trip. Actionlint and whitespace checks pass.
No application code or test assertion changed; no local reinstall is needed. No application change, local reinstall or main-branch
push is needed. Fresh local status is connected with `artwork.framing.v1` and
**voice/screen sharing active**; no restart or deployment is performed.

An isolated QtKeychain/libsecret probe creates only synthetic credentials and
closes its own Secret Service session through the owning GLib D-Bus connection.
Reads still succeed, including a second entry; both synthetic entries are removed.
This does **not reproduce** the observed expired-session error or establish a
recovery fix. Probe and real-keyring evidence are retained alongside CI evidence.
Do not add speculative retry/reset logic without a faithful failing regression.

**Next:** inspect remote CI `38545670054` and Android `38545669926` for87822ff
(the CI watch is still running and writes `ci-fonts-watch.log`), then implement/test stale-keyring-session
recovery and complete live retained-source/Reframe acceptance after a designated
test channel is supplied. Existing physical-device, human acceptance and later
expansion gates remain open. Earlier checkpoint details below are historical.

## Latest checkpoint and next work — 2026-10-14

Current source commit: `2b33fb943844cf7e457a456796d20e6c81e5223b`, pushed to
`main` and `feature/desktop-accessibility`. All 50 pending desktop/test/doc files
were committed. User confirms main pushes auto-deploy; the explicit commit/push
instruction superseded the previous deployment pause. Backup remains unverified.
No manual deployment was invoked.

Coolify webhook `rpjdbnvdffrfvdid8mwhsvq5` finished at **05:20:03 UTC** for this
exact source commit. Docker and both Android workflows succeed. **Both CI runs
fail**; inspected run `38516037816` reports one failing test out of 202:
`ArtworkCache.StorePruningRejectsIncompleteDirectoryEnumeration`, assertions in
`tests/unit/test_artwork_cache.cpp` at lines 770/773/774/777. Root cause is not yet
investigated; do not call the full checkpoint green. Failure log is retained at
`build/desktop-artwork-rollout-preflight/ci-failed.log`.

The connected live daemon advertises `artwork.framing.v1`, supporting that shared
artwork backend behavior is deployed. The actual running image digest/revision
has not been inspected on the host. Latest health/client observation is connected
with no error; these facts are volatile and must be rechecked.

Local application source matches the already-installed shared-artwork snapshot;
only documentation and integration tests changed afterward. All **127 installed
artifacts** and both running binaries match the validated packages; pacman reports
zero altered files. No local rebuild/reinstall was needed.

After deployment the old daemon could not read saved credentials, logging
`keyring unavailable: The session does not exist`; both accounts became
`login_required`. Restarting only `omachat.service` at **05:22:39 UTC** restored
both accounts from saved credentials. GUI was mapped/visible with title
`#general — OmaChat`. Evidence points to a stale QtKeychain/Secret Service session;
automatic recovery is not implemented or claimed. No credential deletion or new
login was needed. Evidence: `build/desktop-artwork-rollout-preflight/local-recovery.json`.

**Next:** investigate and fix the failing CI regression first. Then address
stale-keyring-session recovery with a reproducing test and verify live retained
source upload/Reframe in a designated test channel. Phase 2 personal organization
and notifications remain open; human acceptance, Android physical-device and
MLS/SFrame gates remain separate. Preserve the ignored transcript/evidence.
This checkpoint update is local documentation, not yet committed or pushed.

## Commit and automatic deployment authorized — 2026-10-14

The user explicitly requests committing and pushing the current work and states
that pushes auto-deploy. Proceed with a normal fast-forward push to main and the
working branch; no manual deployment is requested. This supersedes the earlier
pause awaiting backup/access and idle activity. Backup verification remains
unestablished, and voice/screen sharing were active at the last observation.
Record actual CI/deployment/health outcomes after the push; authorization alone
is not evidence that the new image or capability is running.

## Hosted rollout preflight; active session — 2026-10-14

Read-only live recheck at approximately 05:16 UTC: Coolify reports running/healthy;
connected daemon still lacks `artwork.framing.v1`. Latest deployment remains
October 13's finished webhook at 83441d0. No scheduled application tasks exist.
**Voice and screen sharing are active**, superseding the previous idle checkpoint.
No restart/deployment/install was attempted. Hosted terminal access or a verified
current backup remains required; neither is established by this continuation.

Registry `latest` still resolves to the intended OCI index digest
`sha256:b280e8128c09fc229bec90748b8677462f4ad670e277be97c9a2eb3bb96d7bad`;
its linux/amd64 manifest is
`sha256:1b1029168b9ca5d0daf6605a080cbe83a4fd2155b2485dcb8cf6e65b34c59d6b`.
Coolify lists persistent volume `32t4edl2sggh7drj9v2ov2qf_omachat-data` mounted at
`/var/lib/omachat`. These are volatile observations, not runtime image proof.

Prepared `docs/shared-artwork-rollout.md`: verify actual hosted paths/image,
wait for idle activity, stop writes, back up and privately verify the entire
volume (database/WAL, attachments, configuration and TLS identity), then pull
and deploy the matching image. Require schema14 backup integrity/FK checks;
post-deploy verify schema15, image revision/digest, TLS identity, live capability,
health and test-channel behavior. Rollback requires the older image plus restored
backup, not merely restarting older binaries against schema15.

This continuation changes documentation only; prior source/transcript edits are
preserved. Earlier test/install evidence remains historical; no new application
validation, deployment, backup verification, commit or publication is claimed.
Resume with hosted access/verified backup and a fresh activity check.

## Artwork preparation lifecycle coverage; rollout backup gate — 2026-10-13

Continued the latest shared-artwork handoff. Three new integration regressions
hold the real worker queue and verify cancellation during preparation prevents
late publication, an upload resumed on a new connection requires an explicit
finish, and management permission revoked during preparation aborts publication.
Each verifies the queued preparation state through a second Finish request's
rate-limit reply. Source/variants are unavailable after cancellation or revocation;
a resumed source becomes available only after explicit finish.

The existing RelWithDebInfo integration target builds successfully. All **10/10
ArtworkFraming tests** pass, including the three new tests and seven existing
artwork checks. Formatting and whitespace checks pass. This is focused test
validation; the earlier full Release/package/native results above are unchanged.
Only integration tests and documentation changed; installed application artifacts
remain the earlier snapshot. No application rebuild/install or source push is
needed for this test-only continuation. Tests remain uncommitted with prior edits.
Evidence: `build/desktop-artwork-lifecycle/evidence/artwork-tests.log`.

Live recheck: Coolify reports running/healthy; the connected daemon still lacks
`artwork.framing.v1`, and voice/streaming are idle. Coolify's latest deployment
`tlv969slqqkwkd2mhxzeuocq` is a finished webhook at 83441d0, from
09:40:55–09:41:08 UTC on October 13. That metadata does not prove the matching
image is running. Runtime digest remains unavailable through the connector.

A new Coolify MCP now exposes deploy/control actions. The previous no-deploy-tool
blocker is superseded, but the application has **no scheduled backup tasks**.
No database backup or hosted filesystem/shell access has been verified. A request
for hosted terminal/SSH access or confirmation of a current verified database
backup is pending. No deployment was queued: schema15 prevents rollback to older
binaries without restoring a schema14 backup. Resume by completing that backup
gate, pulling the matching published image, and verifying capability and health.
The deploy action's token ability is not yet proven by an actual mutation.
Live facts are volatile. No public release, AUR publication or vault commit.

## Shared artwork framing installed locally; hosted rollout pending — 2026-10-13

Managers can opt into **Keep the full image for managers to reframe**. New image
selections start unchecked; ordinary crop-only uploads retain their contract.
Reframe retrieves a private retained source, restores its source-pixel crop and
requires explicit review/upload. Creation staging preserves the chosen source
and framing under its existing account/server-bound opaque token. Clearing the
retention choice produces a crop-only replacement. Legacy assets explain that
no retained source exists. Server settings now expose `serverDetails()` to QML;
the independent native entry-button check caught and corrected its previously
empty form. Reframe controls reveal themselves on keyboard focus.

`artwork.framing.v1` stores immutable slot-specific source dimensions and crop
rectangle (normalized centre = focal point), rather than UI crop-travel values.
The server generates 512×512/1024×256 crops and 128×128/512×128 variants. Desktop
icon-only requests select SMALL, while banners/shared slots keep FULL; old assets
and cached full rasters retain valid fallback. SOURCE is management-only, with
per-chunk authorization and current-identity checks. No public image URL exists.
Metadata and unknown framing fields are removed. Static bounded sources are
limited to 2 MiB, 8192 pixels/dimension and 16 megapixels; each output is ≤2 MiB.
Opaque sources may be JPEG-recompressed; retained rasters are not original file
bytes. Two bounded server workers perform preparation, followed by live permission
checks and atomic publication/cleanup. See docs/shared-artwork-framing.md.

Independent review found a pre-existing upload-carrier ownership bug. Schema
**14→15** now transactionally makes carriers nullable, preserves server artwork
through carrier and final-channel deletion, retains message deletion cascades,
preserves emoji rows/indexes and reclaims unreferenced detached artifacts.
Foreign-key checks run before commit, and enforcement is restored on success and
rollback. Dedicated regressions verify these lifetimes and failed migration
rollback. A server with no channels retains display/source access; its existing
upload contract still requires a channel before saving new artwork or reframing.

Warnings-as-errors Release and fresh Arch package each pass **440/440 checks**
(430 non-voice with the real secure-keyring round trip, plus 10 serial voice with
compilation idle). Both builds pass **27/27 native Wayland checks**. Both QML lint
targets pass with existing warnings; formatting and whitespace pass. Captures
cover 720×460/150% and 1440×900/100%, dark/light, restored shared framing, reachable
retention/actions and a real keyboard Reframe action selecting pixels omitted
from the earlier crop. Independent review passes after the lifetime correction.
Initial fixture, lite-protobuf API and new-test compiler failures are preserved
under build/desktop-shared-artwork/; subsequent corrected builds/checks pass.
Automated/native checks remain distinct from human usability acceptance.

Backend **83441d0a5a894276bcd53f54d2ef6da64a40ccd6** is committed and pushed to
feature/desktop-accessibility and main; client/prior desktop edits remain
uncommitted. All five exact-SHA CI/Android/Docker workflows pass.
Published image revision matches; digest **sha256:b280e8128c09fc229bec90748b8677462f4ad670e277be97c9a2eb3bb96d7bad**.
No forced hosted deployment was performed. The connected live endpoint still
lacks `artwork.framing.v1`, while Coolify reports running/healthy. The current
connector has no deploy action, the local MCP credential does not authenticate
to the deploy API, and no browser provider is connected. A request for a browser
or deploy-capable local API access is pending. These live facts are volatile;
recheck before acting. Do not claim the new backend or capability deployed.

Arch main/debug **0.2.2-1** are installed from the **442-file** uncommitted snapshot
based on 83441d0. Source manifest SHA-256:
`af70565b3d26b864e9eea22e93c7aac90193285a08b9abcfae75c0f3a4e2c4ef`.
All **127 installed files/symlinks** and both running binaries match; pacman
reports zero alterations. The client is connected and visible. Voice, streaming
and transfers were idle before upgrade. Later source changes are documentation
only. No client/vault commit, public versioned release or AUR publication occurs.

**Resume:** supply/confirm deploy access, use the published matching image,
perform normal schema-14 database backup before rollout (older binaries reject
schema 15), and verify rollout/health plus the live capability. Then consider
preparation cancellation/reconnect/permission-revocation coverage (independent
review suggestion), Phase 2 personal organization/notifications and human
acceptance. Android physical-device and MLS/SFrame gates remain separate.
Evidence: build/desktop-shared-artwork/evidence/, github-runs.json and native logs.


## Device-local artwork crop memory installed — 2026-10-13

Reselecting the same canonical image pixels restores the last approved crop
travel fractions and zoom for that account/endpoint/server/slot, including after
client restart. Creation remembers an accepted Use crop; existing channel/server
artwork remembers a successful upload/update. Reset/Cancel and rejected crops do
not replace the accepted choice. Late preparation and cache-epoch renewal never
overwrite newer keyboard/drag/zoom edits or retained conflict drafts. Restored
choices still require explicit review and upload.

The private XDG data store contains only opaque SHA-256 keys, normalized travel
coordinates, zoom and save times; no source paths, image bytes or identity text.
Limits: 128 choices, 64 KiB file, 90 days; expired/future/malformed rows are
ignored and pruned on a later save. Atomic writes and cross-process locking merge
concurrent choices. Unsafe leaf paths/permissions, corrupt stores and unknown
versions fail closed. Reads/writes are off-thread and queued writes are bounded.
Preference saves are best-effort on I/O/lock/busy failure; they never turn an
otherwise successful upload into an error. Same-user filesystem substitution
races are not an atomic adversarial filesystem boundary.

This is device-local crop memory, **not completion of shared focal metadata and
server-generated variants**. Originals remain on the user's device; the server
still receives the canonical cropped raster. Current x/y values describe crop
travel, not absolute source-image focal coordinates. Shared original retention,
wire/schema metadata, authorized variants and slot selection remain open.

Warnings-as-errors Release and clean Arch package each pass **424/424 checks**
(414 non-voice including the real secure-keyring round trip, plus 10 serial voice
with compilation idle). Release/package native Wayland each pass **21/21**;
compact 720x460/150% and wide 1440x900/100% dark/light captures verify restored
framing, readable feedback, Reset keyboard behavior and bounded actions. Both
QML lint targets and formatting/whitespace checks pass with existing lint
warnings. Native compositor geometry allows one-pixel rounding. Automated checks
and inspected captures remain separate from human usability acceptance.

Early overlapping build orchestration was stopped; its missing-object linker
failure is retained and subsequent builds are serialized. Four initial native
failures came from the existing exact-size fixture; a one-pixel tolerance fixes
it. Initial captures caught the opening fade; final captures use reduced motion
and wait for full opacity. Original logs remain in build/desktop-crop-memory/.
The first broad pass exposed a gesture fixture that assumed every crop reopened
centered. It now explicitly presses Reset before motion checks. All four crop
fixtures isolate the preference store; a test-only spill from the earlier
Operations fixture was moved out of application data into ignored evidence.
Two first package-native checks failed to obtain focus, followed by an existing
server-crop exact-height assertion. A shared test harness now identifies each
owned window by a unique title, dispatches to its exact compositor address and
checks focus/geometry with bounded retries and one-pixel rounding tolerance.
A later package capture received a deferred 128-pixel height configure. The
capture harness now fixes the requested size and waits for multiple stable Qt
and compositor geometry/focus observations without reissuing float commands.
Later diagnostics showed the native window could lose activation between
key inputs while the QML dialog retained focus. The test harness now reacquires
its own window before keyboard input; production shortcut contexts are unchanged.
Final focused package/native runs include these corrections. Application source
stays frozen; full package regression/voice validation predates this native-only
test-harness correction, with identical packaged application artifacts.
Self-review is recorded; no independent subagent review is claimed.

Main/debug **0.2.2-1** are locally installed from the **437-file** uncommitted
snapshot based on **5708472**; source manifest SHA-256:
`4259a775532aaf958b99b12f15320132ded8b0307c17df7f1f296fbdc96acdca`.
All **127 installed files/symlinks** and running binaries match the
packages; pacman reports zero alterations. The client is connected and visible.
Voice, streaming and transfers were idle before upgrade. No backend/protocol
change or hosted deployment was needed for this pass. Application code matches
the installed snapshot; subsequent edits are documentation only.

Prior client edits and the ignored transcript are preserved. No client/vault
commit, public release or AUR publication occurred. Resume this handoff; remaining
shared focal metadata/variants, later expansion phases, Android physical-device
and MLS/SFrame gates remain open. Evidence: build/desktop-crop-memory/.


## Named role and creation previews; whole-list placement installed — 2026-10-13

Managers can review named winning role contributors, existing members and a
hypothetical role consisting of the default role plus one selected role.
Ordinary self-review retains generic sources. Role-only subjects have no real
member, owner bypass or personal overrides; Administrator roles retain their
real enforcement behavior. Proposed channels use the same evaluator as actual
channels, including inherited category/member overrides, with type-filtered GUI
rows. Categories preview top-level placement. Previews reflect current policy;
they do not lock it against later edits or introduce draft permission overrides.
Older-server creation fallback is explicitly a limited current-account estimate.
Supported-server errors clear rows. Names are plain text; drafts and account,
server, subject, snapshot, deletion and offline guards remain intact.

Placement now checks an opaque whole-server structural token as well as source
parent/position. Create, duplicate, delete, move and reorder rotate it; restart
rotates its epoch, preventing ABA reuse. Hidden siblings contribute without
exposing their IDs/names. Complete authorized snapshots refresh after structural
changes. Partial channel events never promote a new token onto an old list.
Drag retains the press-time original; keyboard helpers use checked placement.
Conflicts retain choices and require explicit reload. Reload waits up to three
seconds for the matching GUI snapshot, with current context/permission checks;
it never inserts stale reply metadata or retries a mutation automatically.
Unrelated structural changes conservatively conflict. Legacy capability fallback
retains earlier behavior. Existing GUI navigation gates for server-wide channel
managers remain; the controller validates current channel management permission.

The creation form now has an explicit keyboard-focused Flickable. Native views
cover compact 720×460/150% dark/light and wide 1440×900/100% dark/light, actual
palette assertions, plain role names and fixed action bounds. Automated/native
checks remain separate from human usability acceptance.

Warnings-as-errors Release and final Arch package each pass **414/414 checks**
(404 non-voice including real keyring, plus 10 serial voice with compilation idle).
Release and package-built native Wayland each pass **25/25** checks. Both QML lint
targets and formatting/whitespace checks pass; lint retains existing warnings.
Generated protobuf emits GCC LTO warnings during packaging; the non-LTO
warnings-as-errors build and exact-SHA clean CI pass. Final native captures wait
for actual loaded rows, verify theme/scale, and allow one-pixel compositor rounding.
A final light-theme correction makes the channel-type radio labels readable.

Backend commits 928983a and corrective 5708472885afe6ed18dde1dd2ce55a26790e0674
are pushed to feature/desktop-accessibility and main. The correction fixes an
indefinitely stale daemon list after create/duplicate/ordinary delete. Its new
cached-list regression fails against the earlier package and passes against the
correction. Original failing logs remain in build/desktop-preview-continuation/.
The earlier 928983a workflows pass; final exact-SHA evidence is recorded below.

All five exact-SHA CI/Android/Docker workflows pass. Matching published image
digest: `sha256:e350716831cdc2b303c345a723ea7e8aca08d2c8e774a3dd06d19f2baf55db90`. Post-publication deployment
`s3scmuuobco7fpuobpuoekei` finishes at 5708472; the application reports
running/healthy. The connector does not expose a running container digest.
These live-state facts are volatile and must be rechecked.

Main/debug **0.2.2-1** packages are locally installed from the **433-file**
uncommitted snapshot based on 5708472. Source manifest SHA-256:
`ec52ce7e9b24816479779f693f683e32cb6094584fb5b7fdb348179b116c1255`.
All **127 installed regular files/symlinks** match extracted packages, pacman
reports zero alterations, and running binaries match. Client/daemon are connected
and the client is mapped/visible. Voice, streaming and transfers were idle before
upgrade. Live read-only checks confirm numeric-self 21-row mask parity, named
role-only access, member/role creation previews, opaque placement tokens,
placement reload and encrypted recovery listing without server channel/message
mutations. Evidence: `build/desktop-preview-continuation/`. Subsequent edits are
documentation only.

Client changes remain uncommitted; prior edits and the ignored transcript are
preserved. No public release, AUR publication or vault Git commit occurs.
Remaining: persisted focal metadata/artwork variants, later channel/UI roadmap
phases and the separate Android physical-device/MLS/SFrame gates. The feature
expansion audit is in docs/feature-expansion-handoff.md; this checkpoint resolves
its previously unrecorded role-preview and placement-revision baseline.



## Parallel continuation installed and deployed — 2026-10-12

The requested coordinator + three-worker pass implements atomic restricted
configuration duplication, checked artwork/placement updates and encrypted
restart-persistent send recovery. Sources, handlers and focused tests are
integrated and locally installed; prior client edits and the ignored transcript are preserved.
This completed pass supersedes the older duplication/recovery/conflict worker queues below.

Release validation passes 394/394 tests (384 non-voice, including the actual
secure-keyring round trip, and 10 serial voice checks with compilation idle).
Native Wayland validation passes 30/30 checks, plus four focused native artwork
checks after the Reload artwork focus-reveal correction. Keyboard Space activates
the focused reload, and viewport assertions prove status/action reachability. Captures cover
compact 720×460/150% dark/light and duplication wide 1440×900/100%. QML lint passes
with existing warnings. Independent reviews identified and corrected missing
TLS peer binding, late-purge acknowledgement recreation, account-switch uploads,
ordinary reconnect cancellation, stale duplication replies, last-artwork draft
loss and the compact duplication form width. Original failing logs remain in
`build/desktop-parallel-continuation/`.

Backend and daemon changes are committed as bc47c15 plus the shared IPC contract
correction 5cf7553, pushed to desktop branch and main. The first commit's clean
CI exposed the omitted shared error header; the corrected SHA is
5cf7553157d2e70a5e2814bbf044c3293d943ffb. Docker and Android checks for that SHA
pass, as do both exact-SHA CI workflows. Published image revision matches,
digest sha256:325dac2929a2d8e8358396a58411dede989379b0dab3ef14a485ba16f8eb540d.
Post-publication Coolify deployment z7y0n51y5c36toz8a5naheda finished at that SHA;
application 32t4edl2sggh7drj9v2ov2qf reports running/healthy. The installed daemon
sees all three new capabilities. The connector does not expose the running
container digest. Live state is volatile; recheck before acting.

Clean Arch main/debug 0.2.2-1 packages pass 394/394 tests (384 non-voice,
10 serial voice) and 30/30 package-built native Wayland checks. All 127 installed
regular files/symlinks match extracted packages, pacman reports zero alterations,
and running binaries match. Client is connected, mapped and visible. Voice,
streaming and transfers were idle before upgrade. Live read-only permission
preview returns 21 source rows matching the snapshot mask; recovery support is
available and recovery.list succeeds without creating a user message.

Client changes remain uncommitted, installed from the 430-file snapshot based on
5cf7553. Manifest SHA-256:
a00b27bab612697bc6f0d8b0d2d9e1d74b430f6b13e0f1e60618c0ba7e3cea47.
Subsequent changes are documentation only. Evidence, logs, captures, package hashes
and installation/deployment checkpoints: build/desktop-parallel-continuation/.
Packaging retains GCC LTO warnings in generated protobuf; the warnings-as-errors
non-LTO Release and clean CI builds pass. No public release, AUR publication or
vault Git commit occurred.

Remaining scope: named role contributors, role-only/full creation previews,
persisted focal metadata/variants and whole-list placement revision checks.
Duplication copies category configuration, not children; the GUI retains the
source category. Artwork CAS compares icon/banner references; placement CAS
compares source parent/position and validates current before target, not every
hidden sibling revision. Recovery is 20 records/128KiB/30 days per account; requires
secure keyring and the original attachment files. Uncertain sends never auto-
resend; certificate changes fail closed. User-dismissal tombstones remain in
memory for the daemon lifetime. Native/automated checks remain separate from
human usability acceptance. Public releases/AUR publication remain separate.


## Permission sources and member previews installed — 2026-10-11

Existing-channel access review now shows server-authoritative permission source
stages. Ordinary members can review themselves; members with server Manage Roles
can preview existing server members only in channels they can see. The same
shared evaluator supplies authorization masks and explanations; ordinary checks
retain the trace-free path. Generic stage labels expose no role identity metadata.
Owners/admins, default/member roles, category/channel everyone/role/member
layers, ungranted permissions and the visibility gate are distinguished.

The additive `permission_preview` capability and wire tags 92/93 gate the new
`permission.preview` daemon method. Numeric self/uncached user IDs reach the
server's authorization check; named references use the existing resolver.
Older servers retain current-user mask review. Supported-server errors, pending
requests, offline state, deletion and account/server changes clear stale rows;
serial/context guards reject old replies. Late snapshots coalesce a fresh request
for still-valid context. The production dialog has a manager member picker,
plain-text source rows and compact keyboard scrolling.

Limits: source stages do not identify named contributing roles. Role-only and
full new-channel creation previews remain open. Queued-reply tests drain actual
responses but do not use a deterministic response gate. Automated/native checks
do not establish human usability acceptance.

Independent review passes after fixes for deletion invalidation, numeric-self
resolution and snapshot cancellation. Clean warnings-as-errors Release passes
**337/337 tests** (327 non-voice + 10 serial voice). Both QML lint targets pass
with existing warnings; formatting and whitespace checks pass. Native Wayland
coverage is **10/10** (4 new + 6 existing permission checks). Inspected compact
720×460/150% dark/light captures show readable source rows, scrolling and fixed
Close. Fresh Arch main/debug **0.2.2-1** packages also pass **337/337 tests** and
**10/10 package-built native checks**. Packaging emits GCC LTO warnings in
generated protobuf code; the non-LTO warnings-as-errors build passes. Earlier
failing tests and their fixes remain in evidence logs.

The backend contract and five integration tests are committed as **16e3ed8**,
pushed to `feature/desktop-accessibility` and deployment branch `main`. Exact-SHA
GitHub **CI, Android and Docker** workflows pass. Published image revision matches
`16e3ed8d3b11ab0ec0465de0081d288878236551`, digest
`sha256:b0ca33d41ffe011c6a4add83e64556ae05a5f03674a6f6702028dd45c14a0dc6`.
Post-publication Coolify deployment **5glthj3nndnmkhqwsxs1g3oe** finished and the
app reports running/healthy. The installed daemon sees the capability. The
connector does not expose the running container digest; do not claim that proof.

Client changes and earlier desktop edits remain uncommitted and locally installed
from the recorded **418-file snapshot based on 16e3ed8**. All **126 installed
regular files/symlinks** match extracted packages, pacman reports zero alterations,
and running binaries match. The daemon connects and GUI is mapped/visible.
Voice/streaming and transfers were idle before deployment/upgrade. A live
read-only numeric-self preview returns all 21 source rows and an effective mask
matching the snapshot. Source manifest SHA-256: `4b7347a03aa0b9f1a05ad5c1ea68155d9bdfb77ea2274e4843d162b31fa158da`.
Evidence: `build/desktop-permission-sources/`. Subsequent source changes are
documentation only. Prior edits and ignored `session-ses_f0f5.md` are preserved.
No public versioned release, AUR publication or vault Git commit occurred.

## Next pass: maximize parallel subagent execution — explicit user direction, 2026-10-11

The user explicitly requests **subagents on the next pass to finish as much as
possible at once**. Treat this as an execution requirement, not a request for
another planning-only turn. Use all available slots: currently the coordinator
plus three implementation workers. Recheck capacity before launch. Earlier
October 4 parallel instructions below remain useful; this section sets the
current queues and supersedes their completed first-wave tasks.

Start with the latest installed/source/deployment facts above, the remaining
channel/UI plans and security/Android handoffs. Preserve the dirty desktop tree
and ignored transcript. Map exact files before launch: every file has one writer,
workers are told they are not alone, and shared-controller/protocol/CMake changes
go through the assigned owner. Never switch the shared checkout under workers.

Launch three bounded, independent implementation lanes together, choosing the
smallest useful end-to-end slices that can coexist after ownership mapping:

1. **Atomic restricted-channel duplication.** Own its server/daemon/protocol
   contract and dedicated tests. Copy configuration and overrides atomically;
   never briefly expose a restricted copy, copy messages/members, or bypass
   management authorization. Coordinate client entry points with the controller
   owner. Backend changes must be committed/pushed to `main` and rollout verified.
2. **Encrypted restart-persistent failed-send recovery.** Scope account/endpoint
   identity, key storage, bounded retention, deletion and uncertain delivery;
   implement the smallest secure persistence slice in separately owned storage
   files and dedicated tests. Controller integration has one owner. Never store
   message content/file paths in plaintext, auto-resend uncertain sends, or
   overwrite newer drafts. A security blocker is a reason to move to a ready
   lane, not to improvise crypto.
3. **Artwork focal metadata/variants or artwork/placement conflict handling.**
   Choose whichever bounded slice can own distinct files/contracts alongside
   lane 1. Keep immediate operations honest and retain local drafts on conflicts.
   If backend files overlap, do an independent client/UI slice first and queue
   contract work behind that file's owner; do not let agents edit the same file.

The coordinator owns integration, shared controller/header edits unless explicitly
assigned, CMake wiring, docs/vault, package provenance and release/deployment
sequencing. Use spare coordinator time for named role/source attribution,
role-only/new-channel previews, or a ready independent acceptance gap. Supply
exact outcomes, file ownership and acceptance checks to every worker. Workers
write distinct test files and run focused checks; only the coordinator drives
shared build directories. When a worker finishes, give it the next ready slice
or an independent review of another lane. Do not idle slots or spawn nested
agents beyond capacity. Android may use a separate worktree if it does not slow
desktop integration; physical-device and MLS/SFrame gates remain explicit.

Integrate often. Run one combined checkpoint rather than duplicate full suites
per worker. Bound total compiler load to the live system profile (recent -j6),
keep timing-sensitive audio checks free of compiler load, and serialize native
GUI capture, emulator/device access, deployment and installation. Inspect actual
compact/wide dark/light views. Independent review, tested package artifacts and
human acceptance are distinct. Install client changes locally; for server changes,
wait exact-SHA Docker publication before forcing Coolify deployment, then verify
health and live behavior/capability. Recheck voice/streaming/transfers and announce
restart impact. Public releases/AUR publication remain separate.

Finish with integrated outcomes, installed versus source-only state, exact test
and rollout evidence, remaining blockers and refreshed handoff/shared vault.
Keep progressing through ready slices within the pass; do not declare later
roadmap phases complete because one prerequisite is finished.

## Store-time artwork pruning installed — 2026-10-11

Store-time artwork cache directory enumeration and sorting now run on the same two bounded process-wide worker slots as lookup, decode and reconciliation. Each store snapshot reserves its incoming bytes and entry, preserves current pins and the checked destination, and applies guarded cleanup in batches of at most 32 candidates per event-loop turn before publication. Competing stores, LRU/pin/staging/access/account changes refresh the budget plan without repeating decode or download; staging ownership and per-ID request ownership persist through retries. Unreadable directory enumeration, changed candidates and insufficient pinned budgets cannot fund admission. Revocation and destroyed controllers cannot publish stale results.

Limits: final filesystem operations remain synchronous; a candidate count does not guarantee latency on slow storage. External writers are not an atomic multi-process budget transaction, and the existing narrow check/unlink same-user race remains. Frequent in-process mutations can cause rescanning. Automated checks do not establish human usability acceptance.

Incremental self-review records no blocker; this is not an independent agent
review. Final warnings-as-errors Release build passes. Final CTest coverage is
**328/328** across a **318/318 non-voice** pass and **10/10 serial voice** pass;
**35/35 cache checks**, **7/7 native Wayland artwork checks**, both QML lint
targets (existing warnings), formatting and whitespace checks pass. Final Arch
package-built validation passes **303 non-voice bundled checks, 10 serial voice
checks and 15 GUI-only checks**, and
**7/7 package-built native artwork checks** pass. The package build starts clean,
then incrementally incorporates the final unreadable-directory regression; the
obsolete snapshot's check phase was deliberately stopped. A first package rebuild
retained obsolete objects because preserved source timestamps preceded object
timestamps; its unit test list exposed the missing new regression before install.
The affected snapshot files were touched and rebuilt. Final packaging uses
--nocheck followed by the full separate package-built checks above. The snapshot
helper now advances timestamps when copied bytes change.
The first broad run has one screen-share audio frequency assertion failure
(about 409 Hz versus 440 Hz), with compiler load present. The isolated rerun,
final serial voice pass and package checks pass without changing voice code.
Scheduling sensitivity is an inference; the original failure remains in the logs.
The first compile also exposed a new fixture's nonexistent getter; it uses the
existing channelDetails API in the final version. Automated checks do not replace
human usability acceptance.

Fresh main/debug **0.2.2-1** packages are locally installed from the recorded
**416-file uncommitted snapshot** based on `d35634a`. All **126 installed regular
files/symlinks** match extracted packages; pacman reports zero alterations.
Running client/daemon match installed binaries, daemon is connected and GUI
mapped/visible. Voice/streaming were idle and no transfers existed before upgrade.
Source manifest SHA-256: `0aab71595e432115688932983264d6a873a6ecf75cdfdbd349778547f7778638`.
Evidence: `build/desktop-store-pruning/`, including self-review, original/final
regression logs, snapshot, packages and installation checkpoint. Application
source matches the snapshot; subsequent changes are documentation only.
Previous edits and the ignored transcript are preserved. No server change,
deployment, commit, push, public release or AUR publication occurred.

Next desktop queues: full permission source/member previews, restricted
channel duplication, artwork/placement conflicts, focal metadata/variants and
encrypted restart-persistent failed-send recovery. Never auto-resend uncertain
sends or persist message/file recovery plaintext.

## Asynchronous artwork reconciliation installed — 2026-10-11

Artwork authorization reconciliation no longer scans directories on the GUI thread. Read-only workers enumerate owned, no-follow directory descriptors and plan revocation, expired staging and budget cleanup. Scans share the two process-wide artwork lookup/download slots; one pending cleanup per controller coalesces changes. The owner applies at most 32 candidates per event-loop turn, checking policy revision and file/directory identity before descriptor-relative deletion. Account, permission, pins, staging and LRU/store changes invalidate old plans; destroyed controllers cannot apply them. Preview access clears immediately while disk cleanup is deferred.

Limits: store-time budget pruning and final filesystem operations remain synchronous. Candidate count bounds work volume rather than slow-storage latency. Same-user replacement between the final file check and unlink is still a narrow concurrent-writer race; this does not claim atomic compare-and-delete. Frequent mutations can require rescanning. Automated checks do not replace human usability acceptance.

Independent review finds no blocker. Warnings-as-errors clean Release build,
**320/320 CTest**, **28/28 cache unit checks**, **6/6 native Wayland artwork
checks**, both QML lint targets (existing warnings), formatting and whitespace
checks pass. A clean Arch package build initially found one stale synchronous
revocation fixture; the corrected final package run passes **305 bundled checks
plus 15 GUI-only checks**, with **6/6 package-built native Wayland checks**.
The staging expiry fixture now flushes writes before setting its old timestamp;
revocation checks retain immediate preview/access assertions and wait for disk cleanup.

Fresh main/debug **0.2.2-1** packages are installed from the recorded **416-file**
uncommitted snapshot based on `d35634a`. All **126 installed
regular files/symlinks** match extracted packages; pacman reports zero alterations.
Running client and daemon match installed binaries; daemon is connected and GUI
mapped/visible. Pre-install voice and streaming were idle, with zero transfers.
Source manifest SHA-256: `90314221fab1b444c158509e05ff10c2d28cbd1652388725a15ee596c56f1656`.
Evidence: `build/desktop-async-reconcile/`. Subsequent changes are documentation only.
Preserve prior edits and the ignored transcript. No server changes, commit, push,
public release or AUR publication occurred.

Next desktop queues: store-time directory pruning, full permission source/member
previews, restricted duplication, artwork/placement conflicts, focal metadata/variants
and encrypted restart-persistent failed-send recovery. Never auto-resend uncertain
sends or persist message/file recovery plaintext.

## Downloaded-artwork validation continuation — 2026-10-11

Downloaded artwork now decodes outside the GUI thread, sharing the cache lookup's
two process-wide worker slots. Completed transfers retain their registered staging
owner while waiting; per-ID requests coalesce instead of downloading again.
Workers validate read-only staging and destination snapshots through no-follow,
directory-relative descriptors. GUI completion rechecks request serial, account
generation, current authorization and inode/nanosecond file identity before
publication, chmod, eviction or rename. A destination created or changed during
validation cannot be overwritten. Immutable valid entries retain their bytes.
Revoked requests cannot return or clear newer requests. Controller destruction
is guarded; abandoned staging retains the existing expiry policy.
Directory reconciliation/pruning and final filesystem operations remain synchronous.

Independent read-only review found no blocker. The warnings-as-errors Release
build, **314/314 CTest**, **23/23 cache unit tests**, the new blocked-worker real-daemon
regression, both QML lint targets (existing warnings), formatting and whitespace
checks pass. **4/4 native Wayland artwork checks** pass, including normal download,
GUI responsiveness while decode is blocked, deduplication, revocation, destroyed
controller and offline restart. Fresh Arch package build passes **299 bundled
checks plus 15 GUI-only checks**; package-built native artwork checks pass **4/4**.
These automated checks do not replace human usability acceptance.

Fresh main/debug **0.2.2-1** packages are installed from the recorded **416-file**
uncommitted snapshot based on `d35634a`. All **126 installed
regular files/symlinks** match extracted packages; pacman reports zero alterations.
Running client and daemon match installed binaries; daemon is connected and GUI
mapped/visible. Voice was joined, streaming idle and zero transfers existed before
upgrade. The announced standard upgrade hook restarted the daemon and disconnected
voice; it was not automatically rejoined. Source manifest SHA-256: `bf831f57d58be621c72e586b7ca22e419b9b510e8e52283c84cc684d0aeb6fbc`.
Evidence: `build/desktop-async-store/`. Later changes are documentation only.
Preserve prior edits and the ignored transcript. No server changes, commit, push,
public release or AUR publication occurred.

## Artwork-cache lookup continuation — 2026-10-11

Cache-hit decode validation now runs outside the GUI thread. At most two worker
jobs are outstanding process-wide; excess IDs retry through deduplicated timers.
Validation snapshots contain account generation, asset identity and inode/size/
nanosecond modification/change-time stamps. Workers use no-follow directory-relative
file descriptors, bounded image decoding and no file mutations. GUI completion
rechecks request serial, authorization, generation and file identity before
publishing a URL or deleting corruption. Repeated pending requests coalesce;
revocation invalidates callbacks even without a whole-account generation change.
Controller destruction is guarded. Offline restart continues to reuse authorized
cached files. Download/store decode and directory pruning remain synchronous;
this slice does not make all cache operations asynchronous.

Independent review passes. Fresh warnings-as-errors Release build and 309/309
CTest, 21/21 focused checks and 6/6 native Wayland checks pass, including deterministic blocked-worker
revocation/controller-destruction and actual offline restart. Both QML lint
targets pass with existing warnings. Clean package build passes 294/294 bundled
checks plus 15/15 GUI-only checks; the package-built native checks also pass 6/6. Fresh main/debug 0.2.2-1 packages are
installed locally. All 126 installed regular files/symlinks match extracted
packages, pacman reports zero alterations, running client/daemon match installed
binaries, and the client is connected and mapped/visible. Voice/streaming were
idle and no transfers existed before upgrade. Snapshot manifest SHA-256:
`2d0f33c3604e5775d2686526bc0e8eb74776eb7bb6488fbdcca62ea242f9a728`.
Subsequent changes are documentation only. Evidence and the exact 416-file
uncommitted source snapshot are in `build/desktop-async-cache/`. Preserve prior
edits and the ignored transcript. No server changes or publication in this slice.

## Current direction

Desktop remains the priority. The latest October 11 slice installs authoritative
permission source stages and manager member previews, with 337/337 Release and
package checks and 10/10 native/package-native permission checks. Backend
16e3ed8 is committed/pushed/deployed with exact-SHA CI/Android/Docker success;
client UI and prior desktop changes remain uncommitted in the installed snapshot.
The client/daemon are connected and visible. Artwork scans remain bounded/off-thread
with guarded cleanup; final filesystem operations remain synchronous.
Standing workflow: install client changes locally; commit/push server changes to
main and verify rollout. Public releases/AUR publication remain separate.
Next pass MUST use parallel subagents per the October 11 execution section above.
Remaining desktop queues: named role/source attribution and role-only/creation
previews, atomic restricted duplication, artwork/placement conflicts, focal
metadata/variants and encrypted restart-persistent failed-send recovery.

## Channel settings navigation and room details installed — 2026-10-07

Channel settings now has keyboard-accessible **Overview**, **Appearance** and
**Permissions** sections. Switching preserves the same name/topic/description
draft, checked-save originals, conflict/reload state and discard protection.
Appearance explains its immediate artwork operations; keyboard focus scrolls the
chosen icon/banner action into view. Permissions shows authoritative current-user
access and opens the existing overrides editor above the retained settings draft;
closing that editor returns focus to settings. Other-member/source attribution
remains open. Save, reload and immediate changes require an active connection;
read-only/offline controls cannot submit edits. Error feedback is bounded with its
full text available on hover, and Save/Close stay fixed in compact layouts.

The joined voice identity bar has a keyboard-accessible **Voice room details**
action for the currently selected server, preserving conversation selection and
voice membership. Other-server rooms explain that their server must be selected.
The shared details view reserves banner space from asset identity, supports
loading/failure/retry/replacement and fixed icon fallbacks, treats names/topics as
plain text and scrolls long topic/description content above fixed Edit/Close.
Account/server/generation changes and model-reset access loss clear the view.
Cached offline details remain readable; editing requires connection. Popup
reopening cannot erase newer context. This is a room-details banner surface,
not focal metadata, server variants or a new voice workspace.

Final warnings-as-errors Release build and **304/304 CTest** pass. Both QML lint
targets pass with existing warning classes (including unresolved RowListModel
metadata at the new model-reset connections); formatting and whitespace checks
pass. **11/11 native Wayland checks** pass against production QML, including a
real server/daemon/controller MainView route and actual channel-deletion reset.
Inspected compact **720×460/150%** and wide **1440×900/100%** dark/light captures
verify artwork fallback, scrolling and fixed controls. Native size assertions
found repeated float dispatch changed geometry; fixtures now float each window
once and retain exact size checks. Automated acceptance is distinct from human
usability acceptance. Independent review has no production blocker.

Fresh main/debug **0.2.2-1** packages are installed from the recorded uncommitted
**416-file** snapshot based on `d35634a`. Package build IDs match the tested
Release artifacts; all **126 installed regular files/symlinks** match extracted
packages, and pacman reports **zero alterations**. Running client/daemon match
the installed binaries, the daemon is active/connected and the native GUI is
mapped/visible. Voice/streaming were idle and no active transfers existed before
upgrade. Evidence: `build/desktop-room-settings/`, including snapshot/package
hashes, full/native/lint logs, review, captures and installation checkpoint.
Source manifest SHA-256: `c9579e4e2c187e4ecbc72e69b0c3eb8a1d78b0a0f07de57d5557fc5541ec0ba5`. Later edits are documentation only.
No commit, push, public release or AUR publication occurred.

### Connection repair during this continuation

The user reported the installed client could not connect. Two live faults were
confirmed: Mullvad blocked LAN traffic to the intentionally mapped server, and
Coolify's OmaChat application became unhealthy. `mullvad lan set allow` restores
LAN reachability while keeping the VPN connected; existing `/etc/hosts` mappings
are preserved. Restart-only Coolify operation
`vctic3wkq0idlbrhivsy5syg` finished on the existing `891e4c2` server revision with
the same persistent data/TLS identity. Chat TCP is reachable, Coolify reports
running/healthy, and the restarted client daemon connects successfully.
No server source or deployment-branch change was required. Earlier DNS-defect
inference was incorrect: split LAN routing is intentional, and a public DNS
address cannot be compared meaningfully with Coolify's internal Docker gateway.
Sanitized evidence is `evidence/connectivity-repair.json`.

Remaining desktop work: full permission source/member previews, restricted
channel duplication, artwork/placement conflicts, focal metadata/variants,
asynchronous directory pruning and encrypted restart-persistent failed-send
recovery. Preserve prior uncommitted edits and the ignored transcript. Standing
client installation/server publication workflow still applies.

## Creation inherited-access continuation — 2026-10-07

The creation form now offers **Review inherited access** for the current account
before submission. Top-level channels use the latest server-reported server
mask with the channel visibility gate; children use the selected category's
reported effective permissions. Categories ignore the category picker. Owners
and administrators bypass overrides; text/voice rows match the selected type.
Invalid, unavailable or foreign-server categories cannot produce a preview.
Connection and permission updates refresh the review and clear stale rows offline.

Opening the review focuses the scrolling viewport, so arrows, Home/End and
Page Up/Down reach the explanation without changing the name field. Create and
Cancel stay fixed outside the form. All supplied text is plain text. This is a
current-user inherited-access preview, not a full audit of who can access the
new channel. Other-member previews and per-permission source attribution remain
open. Creation adds no private overrides, and the server can apply changed
permissions before accepting the request.

Validation: independent review passes; the fresh warnings-as-errors Release Arch
build succeeds, and the final package build passes **293/293 CTest**. Both QML
lint targets pass with existing warnings and no new creation-dialog warnings;
changed C++ formatting and whitespace checks pass. Package-built native Wayland
permission checks pass **6/6**. The new production creation check verifies actual
viewport focus, keyboard scrolling, live permission changes and offline clearing.
Inspected **720×460/150%** dark/light captures include pixel checks proving the
palette differs. Existing permission review also covers compact/wide dark/light.
The extracted package client launches and captures its startup surface under
Wayland; that smoke check alone does not demonstrate connected workflows.

The keyboard regression initially failed: ScrollView's focus scope retained the
name text field as its focus leaf. Explicit focus handoff when opening the review
fixes this, and the retained GTest assertion verifies both leaf focus and scroll
movement. Loading a test palette after QML creation requires emitting the theme
change signal; capture assertions compare RGB values at 8-bit precision.

Fresh main/debug **0.2.2-1** packages are installed locally. All **126 regular
files/symlinks** match extracted artifacts, and package integrity reports **zero
alterations**. The running client and daemon executables match the installed
binaries; the daemon is active/connected and the native client mapped/visible.
Voice and streaming were idle and zero transfers were active before the upgrade.
No server change, deployment, commit, push, public release or AUR publication
occurs in this slice. Source remains uncommitted based on `d35634a`.

Evidence: `build/desktop-access-review/` contains the reviewed incremental diff,
build/failure/regression/full-suite/lint/native/install logs and inspected captures.
`install/source-manifest.json` records the 414-file snapshot; its SHA-256 is
`7b58c4aab6b26f1663a0a2b29a34d4c6261e40dfe07e01ab535cfdaa03a36766`.
`evidence/installation-checkpoint.json` records package hashes, runtime and file
verification. Application files match the snapshot; subsequent changes are handoff
and roadmap documentation plus test formatting only. Preserve previous edits and
the ignored session transcript. Full creation source/member access review,
restricted duplication, artwork/placement conflicts, focal metadata/variants,
voice-room banners and encrypted restart-persistent recovery remain open.

### Previous installed foundation

Implementation `97f3b43` is independently reviewed, committed, pushed to
`feature/desktop-accessibility` and `main`, and installed locally as fresh
main/debug 0.2.2-1 packages from its git archive. The native client is running;
126 installed files/symlinks match artifacts and package integrity reports zero
altered paths. Final warnings-as-errors build and 257/257 CTest pass; both QML
lint targets and all tracked C++ formatting pass. Both new pending-invalidation
regressions also pass under native Wayland. Package/source evidence is retained
in `build/desktop-install-97f3b43/` and
`build/desktop-phase1-validation/evidence/precommit-source-checkpoint.json`.

Packaging correction `647fc1e` declares `qt6-imageformats` for WebP and is
installed locally; its binaries are byte-identical to reviewed `97f3b43`.
The refreshed package metadata/artifacts are verified without recompilation or
another broad test run; the focused format check passes. Evidence:
`build/desktop-install-647fc1e/`.

Server dependency correction `1aebabf` includes the WebP decoder in Docker.
It is pushed to both branches. Its image publication succeeded, and the final
post-publication Coolify deployment `ae8sut7cl2vq0tuaymom96ei` finished healthy.
The client is connected and the live server reports `channel.placement.v1`.
Initial webhooks preceded publication, so explicit post-publication pulls were
required. Android CI passes at `1aebabf`, and clean-runner native unit/fuzz checks pass
with the WebP dependency. Native integration identified two fixture assumptions
that Return activates ordinary Qt buttons under every platform theme. Blank-theme
runs reproduce both failures; the fixtures now use portable Space activation,
with enabled/focus preconditions and all original saved-result checks retained.
Application code is unchanged. Both repaired fixtures pass blank-theme and native Wayland checks; the full
257/257 suite also passes with the default Qt theme. Final native, Android and Docker CI pass on `aee3e7e`. The test correction has independent review approval. Prior native GTK Return
acceptance remains recorded in the original Wayland evidence.

## Desktop continuation — 2026-10-06

Three bounded parallel lanes add pointer channel placement, server artwork
cropping and checked channel detail saves. Parent owns controller declarations,
test wiring, real-daemon GUI coverage, compact long-content clipping and release
coordination. All previous work and the ignored session transcript are preserved.

- Sidebar drag uses an eight-scaled-pixel threshold, visible before/after/end
  cues and the existing atomic placement request. Channels move into/out of
  categories; categories reorder among categories. Invalid/offline/unauthorized
  gestures and Escape cancel. Pending/rejected requests keep server order.
  Gestures stay within the visible list; no edge scrolling is implemented.
- Server icon/banner selection opens the existing crop review. It preserves
  bounded async decode, canonical metadata-free PNG output, private temporary
  ownership and pending/error feedback. Fingerprints now bind selected server
  as well as source/account/generation. Authority is checked again before upload.
- `channel.details.cas.v1` compares original name/topic/description atomically
  after permission checks and before mutation. Conflicts retain local input.
  Reload latest fetches authoritative values and retains the draft for deliberate
  review/save. Dirty remote events never rebase originals. Callback generation,
  account and server guards protect reopened/different-context editors. Legacy
  wire updates remain compatible; checked desktop saves require this capability.
- The latest-value comparison scrolls inside the form, and the nested description
  viewport clips long text. Save/Close stay fixed in compact layouts.

Source verification: warnings-as-errors Release build, **276/276 CTest**, both
QML lint targets (existing warnings), tracked/new C++ formatting and independent
review pass. Production Wayland operation checks pass **5/5**; server crop checks
pass **6/6**, including canonical uploads and context rejection. Inspected drag,
detail and server crop captures
are 720×460/150% and 1440×900/100% in dark/light palettes. These are automated
checks, distinct from human acceptance. Installation/deployment outcomes are
recorded at the final checkpoint. Evidence is retained in
`build/desktop-next-validation/evidence/`. Dedicated tests cover target resolution,
context invalidation, canonical server crops, permission-first conflict handling,
explicit reload/retry, stale replies and actual production pointer delegates.

Local installation checkpoint: fresh main/debug **0.2.2-1** packages built from
an exact `891e4c2` git archive pass **265 package checks plus 11 remaining GUI
checks**. All **126 installed regular files/symlinks** match extracted artifacts;
`pacman -Qkk` reports zero alterations for both packages. The updated native
Wayland client is mapped and visible; the user daemon is active and connected.
The pre-upgrade check found voice/streaming idle and zero active transfers.
Package logs/hashes are retained in `build/desktop-next-install/`; installation
proof is `build/desktop-next-validation/evidence/installation-checkpoint.json`.

Server rollout checkpoint: the Docker image for `891e4c2` published successfully,
then forced post-publication Coolify deployment `qqlnq2t2hjubhvwjmwxywiwe` finished
with the application **running:healthy**. Logs confirm image pulling and new
container creation. The connected local client reports `channel.details.cas.v1`.
Published digest:
`sha256:a0e9af3347c76f51bd8fb3f8f3f6064a6146968b7b72a0c29e13ecd0da02e16f`.
Coolify's API does not expose the running image digest; published metadata,
post-publication pulling, health and the live capability are the available proof.
Evidence: `build/desktop-next-validation/evidence/deployment-checkpoint.json`.

Final exact-source CI at `891e4c2` passes on both desktop and `main` branches.
Main results: [native CI](https://github.com/Sleepy-Studio/OmaChat/actions/runs/37517626807),
[Android](https://github.com/Sleepy-Studio/OmaChat/actions/runs/37517626841),
and [Docker publication](https://github.com/Sleepy-Studio/OmaChat/actions/runs/37517626858).
A subsequent documentation-only commit on the desktop branch records this
checkpoint; installed application source and deployment branch remain `891e4c2`.

Remaining desktop scopes: initial-creation crop/access review, persistent focal
metadata/variants, permission-source/other-member previews, atomic restricted
channel duplication, artwork/placement conflict contracts and encrypted
restart-persistent send recovery. Android physical audio and MLS/SFrame remain
independent gates; desktop voice/streaming acceptance remains user-confirmed.

## Desktop continuation — 2026-10-07

Three parallel implementation lanes continue the installed desktop foundation.
Parent owns controller declarations/context invalidation, aggregate creation
completion, CMake wiring, production long-list checks, integration and packaging.

- Initial channel icon/banner pickers use the existing async crop review.
  Approved pixels are canonical metadata-free PNGs held in private temporary
  files. The form receives opaque tokens bound to account, server, access
  generation and image kind. Discard, replacement and context changes release
  unused owners; upload callbacks retain owners through daemon completion.
  Failed creation retains the approved crops for deliberate retry. Partial
  success reports that the channel exists, disables repeat Create and directs
  artwork recovery through Channel settings. Dirty creation drafts ask before
  discard; pending creation cannot close or repeat.
- Sidebar placement scrolls at an in-bounds top/bottom edge at bounded 30 ms
  intervals and at most 12 scaled pixels per tick. It recomputes the destination
  under a stationary pointer. One source delegate is retained through the grab,
  with no full-list cache expansion. Bounds, release, Escape, off-list movement,
  center movement and context/authorization changes stop scrolling. The bottom
  strip supports explicit end placement; server order remains authoritative.
- Quick-switcher text/voice results show lazy visible-row channel artwork with
  fixed type-icon fallbacks. Names/details remain plain text and elide, and the
  result viewport fits the compact window. Existing joined-voice controls show
  channel artwork/fallback without re-requesting it on routine voice telemetry.
  No separate voice-room view exists; a room banner remains future work.

Validation: a fresh warnings-as-errors Release configure/build and independent
read-only review pass. The final Arch source snapshot builds fresh main/debug
**0.2.2-1** packages; **291/291 CTest** pass from that package build, including all
GUI tests. Both QML lint targets pass with existing warnings; changed C++
formatting and whitespace checks pass. Package-built native Wayland checks pass
**13/13**: six creation/crop cases, four artwork identity cases and three
production crop/pointer cases. Crop captures are compact **720×460/150%** and wide
**1440×900/100%** (one-pixel compositor rounding permitted), dark/light. Switcher
and joined-voice captures are **720×460/150%**, dark/light; controls and labels
are inspected. The extracted package GUI also launches/captures under Wayland.
Automated checks remain distinct from human acceptance.

Test reliability corrections: production mock signal listeners must target the
same mock as its action/property references. Asynchronous checks inside GTest
use GTest-compatible event-loop assertions; Qt-only QTRY macros can return
without recording a GTest failure. Native shortcuts require both internal focus
and an active compositor window. Crop closure restores form focus after its
transition, and fixtures assert this handoff before Escape.

Local installation: both fresh main/debug packages are installed. All **126
regular files/symlinks** match extracted artifacts; `pacman -Qkk` reports **zero
altered paths** for both packages (39 main/160 debug total paths). The daemon is
active/connected and `/usr/bin/omachat` mapped/visible. Voice had become joined
before upgrade; the announced service restart disconnected it. No streaming or
transfers were active; voice is currently disconnected and requires user rejoin.
The existing nonfatal portal app-ID registration warning remains.

Source provenance is `build/desktop-oct7-install/source-manifest.json`, with its
SHA-256 and base revision in
`build/desktop-oct7-validation/evidence/source-checkpoint.json`. Package artifacts,
build/full-suite/lint/install logs are in `build/desktop-oct7-install/`; native
logs, inspected captures and installation proof are in
`build/desktop-oct7-validation/evidence/installation-checkpoint.json` and its
sibling evidence files. The application source remains uncommitted on
`feature/desktop-accessibility`. No commit, push, hosted deployment, public release
or AUR publication occurs in this continuation. Preserve all edits and the
ignored session transcript.

Next desktop scopes: full creation inherited-access review, permission-source
and other-member previews, atomic restricted-channel duplication, artwork and
placement conflict contracts, persisted focal metadata/variants, a distinct
voice-room/banner surface, and encrypted restart-persistent failed-send recovery.
Android physical audio and MLS/SFrame remain independent gates; existing desktop
voice/streaming acceptance remains user-confirmed.

## Channel foundation continuation — 2026-10-05

- Exact placement opens a keyboard/pointer dialog from channel/category menus,
  selecting a destination category and a visible sibling or end. Pending saves
  block repeat/close; rejection retains choices; feedback stays above the footer.
  The additive `UpdateChannel.before_id/set_before` fields let the server resolve
  current order, including hidden siblings and interleaved category positions,
  before the existing checked transaction. Missing/moved/inaccessible/self or
  wrong-type targets reject without a mutation. Existing numeric placement stays
  compatible. CLI: `omachatctl channel place CHANNEL CATEGORY_ID [BEFORE_CHANNEL_ID]`.
- Access and permissions opens for members as well as managers. Your access shows
  the latest server-reported effective permission names for the current account,
  with known category hierarchy and an explanation of precedence/owner bypass.
  Server-wide actions are excluded. Offline or unavailable results are explicit;
  the view makes no per-bit source attribution or previews for another member.
  Managers can switch to the existing override editor. The review scrolls with
  arrows, Page Up/Down, Home/End and pointer input.
- Choosing channel settings artwork opens an icon/banner crop review. Pointer drag,
  arrows, Shift+arrows, zoom and Reset select the region. Decode/transform jobs run
  off the GUI thread, with at most two outstanding jobs. Static PNG/JPEG/WebP inputs
  are bounded to 2 MiB, 8192-pixel edges and 16 megapixels. Canonical metadata-free
  PNG output is 512×512 for icons or 1024×256 for banners, also at most 2 MiB.
  Bounded preview pixels preserve original aspect geometry. Pixel/account-scope
  fingerprints reject source replacement or preview reuse across accounts; stale
  worker completions use guarded GUI callbacks. Private temporary crops stay owned
  through daemon completion. Pending uploads block repeat/close; failure stays in
  the crop dialog. This uploads a flattened crop, not persisted focal metadata or
  multiple server-generated variants. Cropping currently applies through channel
  settings, not the initial creation picker or server artwork.

Validation: fresh Release configure/build completes; the full **255/255 CTest**
suite passes. Both QML lint targets pass with existing warnings. Final focused
checks pass 18/18, followed by 5/5 strengthened crop/permission interaction checks
and the final placement check. Native Wayland checks cover all three new dialogs,
sidebar keyboard behavior and source/account crop guards. Final crop drag/zoom/
Reset and placement post-save/nested-dropdown Escape refinements also pass native
checks. Compact/wide captures are **720×460 at 150%** and **1440×900 at 100%** in
both dark/light themes; inspected captures retain readable controls and fixed
feedback. The last full suite preceded the final placement Escape refinement;
its focused offscreen and native checks pass after that refinement. The isolated
checks use unique fixture accounts, TLS servers, daemons and temporary cache roots;
no live account or phone actions. This is automated acceptance, not human review.
Evidence and source hashes: `build/desktop-phase1-validation/evidence/`.
This source checkpoint preceded packaging, installation and publication. The
standing workflow above authorizes those steps after release verification.
Pre-commit review additionally requires pending placement/crop dialogs to cancel
on artwork generation changes, superseded completion guards, and selected-server
checks before deferred crop submission; focused regressions cover both dialogs.

Remaining ready slices after the October 6 continuation: permission-source/other-member
and creation previews, atomic restricted-channel duplication, artwork/placement
conflict handling, persisted focal/variant metadata, and encrypted send recovery.
Android physical audio and MLS/SFrame remain independent gates. Desktop voice and
streaming retain user-confirmed acceptance; automated UI checks remain distinct
from independent human acceptance.

## Committed continuation installed — 2026-10-04

At the user's explicit request, implementation commit `f285540` is locally
committed on `feature/desktop-accessibility` and fresh main/debug **0.2.2-1**
packages built from its git archive are installed. Independent pre-commit review
identified and cleared the cache-root substitution defect; pruning and registered
staging cleanup reject substituted root/staging directories. Both new regressions
fail against the previous implementation and pass the fix.

The final Release suite passes **238/238** tests, including **15 cache** checks.
Both QML lint targets pass with existing warnings. The clean package build passes
**232 package checks plus 6 theme checks**. Package hashes/provenance and logs are
retained in `build/desktop-install-f285540/`; **124 installed regular files and
symlinks** match extracted artifacts and package integrity reports zero altered
paths. The updated native Wayland client is mapped and the daemon is active and
connected. Voice was not joined during the upgrade.

A subsequent documentation-only commit records this outcome; its code matches the
installed implementation commit. No push, merge, public release, AUR update or
hosted server deployment occurred. The locally installed server binary includes
the category safeguard; the hosted server still needs deployment.

## Parallel desktop continuation — 2026-10-04

- Shared theme tokens keep normal/action text readable on normal, hovered and
  selected surfaces. Focus and control boundaries have measured colors; primary
  hover/pressed fills preserve text contrast. Semantic fills and avatar initials
  use contrasting text. TriState supports keyboard arrows/activation. Disabled
  controls remain deliberately dimmed; decorative borders remain quiet.
- Member/channel lists expose Menu/Shift+F10 actions and keypad Enter. Category
  activation toggles collapse. Long search metadata/channel headings elide;
  profiles scroll within the window. Offline members retain readable names and
  focus. Topic editing has a keyboard-reachable control. Menu actions defer new
  popups until closure so focus restoration cannot compete with the new popup.
- Artwork has a private hashed account/endpoint/username/self/certificate-pin
  disk scope, a global completed-file budget of **64 MiB/256 entries**, authorized
  model reconciliation, replacement/revocation removal and protected active
  files. Unknown startup authorization waits for the snapshot before purging.
  Decode/byte checks reject corrupt entries; substituted root/scope/staging symlinks are
  rejected. Sixteen download slots admit excess requests through deduplicated
  timers. Supported server artwork is limited to 2 MiB; active staging can add
  up to 32 MiB separately from the completed-file budget. Abandoned staging is
  removed after one day. Lookup still performs one bounded synchronous decode;
  generic daemon media caching is outside this artwork slice.
- Nonempty category deletion reparents children at the end of the top-level list
  with dense positions inside a checked SQLite transaction. Messages and child
  overrides remain. Every child's management permission is required unless the
  actor has server authority. **Nonempty categories with any inherited override
  are rejected** with instructions to review/move children first, preventing
  silent widening of access. Storage commits precede state/events; observers get
  deletion and permission resync. Confirmation explains the behavior and starts
  on Cancel. The hosted server needs this source update before it gains the fix.
- Restart-persistent failed-send recovery is scoped in
  [desktop-send-recovery-design.md](desktop-send-recovery-design.md). It remains
  unimplemented. Plaintext LocalStore is unsuitable for content/paths; establish
  keyring size/failure/ordering contracts before persistence-before-send wiring.

Verification: a clean Release configure/build and the final **238/238 CTest**
pass, including **15 cache**, **4 theme-state**, **4 category deletion** and
**16 production-operation** tests. Both QML lint targets pass with existing
warnings; whitespace checks pass. The native Wayland conversation, oversized
search metadata and sidebar-menu/confirmation flows pass, as does the compact/
wide dark/light matrix at 150%/100%. Native compact captures are 720×460;
the compositor rounds the requested 1440×900 wide window to 1441×900, which the
harness now records with a one-pixel tolerance. The real shared-control audit
also passes all 22 installed palettes under Wayland. Inspected native captures,
logs and source provenance are retained in
`build/desktop-continuation-validation/evidence/`. The initial confirmation test
was corrected to wait for popup opening/focus; menu popup ordering was hardened.
The oversized profile fixture now respects the server's 300-character bio limit.
All verification uses isolated fixture accounts/servers/daemons. This is automated
acceptance, not independent human acceptance or physical Android validation.

The source continuation was subsequently committed and installed at explicit user
request, as recorded above. No push, merge, release, AUR update or hosted deployment
occurred. Earlier package artifacts are superseded by the committed build.

Next ready slices: artwork crop/focal points and variants, effective-permission
review, pointer placement, atomic restricted-channel duplication/conflict-aware
edits, and the scoped encrypted send recovery. Android physical audio and MLS/
SFrame remain independent gates. Do not treat automated UI checks as human
acceptance or renew the completed desktop voice/streaming acceptance task.

**Voice and streaming are done for the current desktop acceptance checkpoint.**
The user confirmed on 2026-10-02 that both work and explicitly marked the previously
proposed voice/streaming validation task complete. Treat this as user-confirmed
functional acceptance; do not keep presenting it as a blocker or repeat that
baseline test by default. Exact test topology, latency, and devices were not
recorded. This confirmation does not imply Android physical audio validation,
forward secrecy, end-to-end voice encryption, stereo, or per-app capture.

## Source, branches, and deployment

- Repository: `/home/howie/Documents/Github/HowieDuhzit/OmaChat`.
- Current branch: `feature/desktop-accessibility`.
- Installed implementation/source checkpoint: `891e4c2`, pushed to both desktop
  branch and `main` on October 6. Subsequent documentation-only commits preserve
  this installed implementation.
- Android checkpoint: `536b857`, with CI repairs through `0206785`, pushed on
  `feature/android-client`. Desktop branch includes equivalent CI repair commits
  `596ee86` and `03b229c`.
- October 4 feedback/administration/artwork and October 5 placement/review/crop
  are included in `97f3b43`; implementation is independently reviewed; the first clean-runner CI exposed
  a missing WebP plugin. Dependency fixes through `1aebabf` are pushed and final
  native, Android and Docker CI pass on `aee3e7e`.
- Local desktop package last verified installed: refreshed `omachat`/`omachat-debug`
  `0.2.2-1` checkpoint `891e4c2`, installed October 6 under standing authorization.
  Installed files match the checkpoint packages; both host package integrity
  checks report zero altered files. The `891e4c2` image publication and post-publication
  server pull are verified; the application is running/healthy and the connected
  client reports both placement and checked-detail capabilities.
- Preserve the ignored local `session-ses_f0f5.md` transcript; do not publish it.
- Android remains incomplete. Its continuation details live in
  `docs/android-session-handoff.md`; do not infer mobile completion from desktop
  voice acceptance.

## Completed desktop continuation

- Appearance tab: live interface scale, reduced motion, Reset, and persistent
  save/error feedback. Compact windows use a section picker instead of clipped tabs.
- `config.set_ui` daemon IPC validates input, saves atomically, and restores
  previous in-memory values on storage failure. Scale/motion survive reload.
- ThemeProvider requires an explicit parent, forcing QML to use the instance
  configured by main instead of constructing a second singleton.
- `Metrics.px()` is a small QML singleton that reads `Theme.scale` within bindings.
  Existing QML metric calls use it so controls resize immediately.
- Startup app scale uses the same app metrics; desktop/Qt display scaling remains
  independent. Reduced motion drives existing transition durations.
- Small Settings/helper/message metadata/composer text uses a stronger token.
  Tested normal dark/light surfaces meet 4.5:1; the full theme/selected/disabled
  state audit remains open.

## Persistent operation feedback — current working tree

- Composer retains upload completion/failure/cancellation until dismissed, in a
  bounded scrolling panel. Upload completion explicitly distinguishes file bytes
  from message delivery. Active progress still comes from the daemon.
- Send operations retain the original text, attachment paths, reply and action
  state. A failed operation does not overwrite a newer draft or restore content
  into a different channel. Copy text and Dismiss remain available.
- Retry requires an explicit rejection, the original account/channel, current
  send permission, and a connected GUI/daemon/server. Pending retries cannot be
  repeated. Transport failures/timeouts have uncertain delivery and offer no
  automatic retry; the UI asks the user to check the conversation first.
- A persistent StatusBanner Review send action exposes unresolved sends after
  leaving the affected conversation. Existing connection/reconnect feedback stays.
- Upload keys include account identity; events from another account cannot collide.
  `transfer.list` discovers active daemon uploads at GUI connection/account switch,
  reconciles stale active rows, and lets newer progress events win over snapshots.
- Failed-send and terminal upload records last for the GUI session, not across a
  GUI restart. Active transfers remain daemon-owned and are rediscovered. Send
  records are capped at 20 unresolved operations; terminal transfer history is
  bounded at approximately 100 rows without evicting active transfers.

## Current slice verification

- Native build passes; **205/205 CTest tests pass**.
- Six new tests exercise the actual AppController against isolated real TLS servers
  and daemons: failed upload/text retention, single-delivery retry, cancellation,
  contextual navigation, uncertain acknowledgement, read-only/account-switch
  guards, and disconnect/reconnect progress through delivery.
- Production Composer QML is loaded against that controller. Pointer Retry at
  150% scale and keyboard Retry at 100% pass offscreen and under native Wayland.
  Narrow composer captures are inspected; retry controls and input remain visible.
- Both QML lint targets pass with existing warnings. `git diff --check` passes.
- No package installation, commit, push, release or hosted deployment.

## Previous accessibility checkpoint verification

- Native build: passes.
- CTest: **199/199** passes, including QML singleton identity, startup preferences,
  live metric changes, engine ownership, dark/light contrast, and real-daemon
  save/reload/input validation/failed-write rollback.
- Both QML lint targets complete successfully; existing warnings remain.
- A temporary native QtTest driver against an isolated real daemon passed
  keyboard scale changes, keyboard reduced motion, persisted preferences, and
  pointer Reset. Real captures checked at 1200×760/100% and 720×460/150% in dark
  and light palettes. Compact controls and Reset remain visible.
- Wayland sandbox launch and Settings capture pass; compositor selected the final
  window size. Nonfatal portal app-ID registration warning remains.
- Temporary sandbox stopped and CI-repair worktree removed.
- GitHub CI on **92ec294 is green**, freshly checked for this handoff:
  [native CI](https://github.com/Sleepy-Studio/OmaChat/actions/runs/36988284367) and
  [Android build/persistence](https://github.com/Sleepy-Studio/OmaChat/actions/runs/36988284433).
- Desktop voice and streaming: user-confirmed working, acceptance task complete.

## Administration continuation — current working tree

- Channel creation starts with type, name and category. Topic, description, artwork
  and the existing access explanation remain under Optional settings. Hiding that
  section preserves its inputs. Create waits for the server result; rejection
  retains all input beside a persistent error. Pending creates cannot be repeated.
- Channel, server and role editors keep dirty input after rejected saves and show
  persistent pending/success/error feedback beside the edited section. Channel
  saves stay open. Accepted channel/server values reflect server normalization.
- Close and Escape ask before discarding dirty settings. Outside clicks cannot
  dismiss a dirty or pending editor. Discard confirmation focuses Cancel and
  returns focus to the editor when dismissed. Pending saves block closing.
- Switching roles asks before losing dirty role input; Cancel retains the role
  and draft. Dirty role state clears after a successful response, replacing the
  old optimistic clearing. Remote changes preserve dirty input; successful save
  replies leave edits made after submission dirty.
- Category and artwork remain immediate operations, explicitly explained in each
  settings editor. Their existing preview, transfer and global-error behavior is
  preserved; this is not a transactional save of artwork/role assignments/emoji.
- Channel settings has a bounded scrolling form with fixed Save/Close actions.
  The compact creation form also keeps Create/Cancel and Optional settings visible.

Native build and **208/208 CTest tests** pass. Three additional production-QML /
real-TLS-server / daemon / controller tests cover rejected creates/details/roles,
retained optional topic, normalized channel names, remote updates, newer pending
edits, discard decisions and role switching. Pointer and Escape checks run
at 720×460 offscreen (creation at 150%, settings at 100%) and under native Wayland;
the compositor chooses the native window size. Captures are inspected. Both QML
lint targets pass with existing warnings; whitespace checks pass. Full-main-window,
light-theme and complete conversation workflow acceptance remain open.

No package installation, commit, push, release or hosted deployment.

## Conversation and artwork continuation — current working tree

- Server/channel banners reserve their existing height from attachment identity,
  including before a preview URL exists. Loading text and an unavailable-artwork
  fallback occupy that space. Failed preview downloads expose Retry; decode errors
  retain the fallback. Category artwork has a fixed icon slot with a hash fallback.
  Server-rail initials and channel-type fallback icons already preserve geometry.
- Preview download failures are exposed to QML separately from successful URLs;
  an explicit retry clears the failed state while downloading.
- Search height is bounded by the current window, with a shrinking, scrolling
  results list. Nine results remain inside 720×460 at 150% scale.
- StatusBanner uses the theme's readable raised surface with a warning border;
  the previous darkened-yellow fill made light-theme text unreadable.
- Production MainView, related QML and native media view types now load in the
  real-daemon/controller test harness, with actual icon resources.

Native build and **210/210 CTest tests** pass. Both QML lint targets pass with
existing warnings; whitespace checks pass. Two new tests cover preview download
and decode failure/recovery with fixed geometry, plus full-window keyboard send,
reply, nine-result search, exact-match navigation, Return to latest, failed-send
Review navigation and pointer Dismiss. The conversation flow passes at 720×460,
150% scale in dark/light palettes offscreen and native Wayland; all four conversation
and search captures are inspected. This is automated native interaction, not
independent human acceptance. Broad role/member/emoji and all-theme state coverage
remain open.

A clean Release Arch package build passes **208/208 package checks**, with the
remaining **2/2 theme tests** separately passing from that clean build. Main/debug
packages, checksums, logs and captures are retained in `build/desktop-checkpoint/`
(ignored), with `checkpoint.json` identifying HEAD and uncommitted status. They
retain `0.2.2-1` for this local checkpoint; this is not the public release artifact.
Package paths for binaries, systemd user service, desktop entry and icon are inspected.
The clean Release executable launches against an isolated real sandbox under native
Wayland; its capture is inspected. The extracted package executable also passes
a live-sandbox Wayland screenshot smoke; both captures are inspected. The sandbox is stopped. A nonfatal portal app-ID
registration warning remains. Live installed packages are still `0.2.2-1` and the
existing `omachat.service` remains active; this source checkpoint is uninstalled.
No installed package, hosted server, commit, push, merge or release changed.

## Compact administration, emoji and focus — 2026-10-03

- Compact role settings uses a role selector and New role action above a scrolling
  form. Save/Delete/Revert remain fixed below the form. Keyboard focus scrolls
  colors and permissions into view; list selection, swatches and member assignment
  chips support keyboard activation with visible focus feedback.
- The emoji picker is bounded and centered in the window overlay for both composer
  insertion and message reactions. Search supports Down to the results and Return
  to choose a match; Unicode/custom grids support arrows, Return and Space.
  Unicode/Server switches are keyboard buttons. Composer focus returns on closure.
- Custom emoji upload uses a name field above the image/upload row; compact emoji
  cells keep their preview, name and Delete control inside the visible grid.
- Opening the members drawer focuses its list. Return opens a profile; closing the
  profile restores list focus. Closing channel/member drawers restores composer
  focus. Empty member lists ignore Return safely.
- Test resources now include the real bundled emoji catalog. Expanded production
  MainView/real-server/daemon/controller tests use a second member and a real custom
  emoji. They check keyboard role selection, color/permission editing and Revert,
  role assignment/removal, custom emoji Delete, emoji insertion, popup bounds and
  drawer/profile return. The matrix covers 720×460/150% and 1440×900/100%, dark/light,
  offscreen and native Wayland. Actual native test-window sizes are set through
  Hyprland's Lua window dispatchers; desktop configuration is unchanged.

Native build and the full **211/211 CTest** pass preceded the final focus/reaction
additions. All **12/12 operation tests** pass after those additions; both QML lint
targets and whitespace checks pass with existing warnings. Native compact/wide
matrix and conversation/reaction flows pass. Captures of roles, focused permissions,
members and custom emoji are inspected. This remains automated acceptance.

The refreshed incremental Release Arch build passes **209/209 package checks**
and **2/2 separate theme checks**. A final assertion/formatting refresh rebuilds
without repeating the unchanged broad suite; all **14/14 Release GUI tests** then
pass against the refreshed source. Packages, source/package checksums, successful
logs and inspected captures are retained in `build/desktop-checkpoint/`, with
`checkpoint.json` and the prior October 2 packages retained for reference.
The extracted final package executable launches against an isolated real sandbox
under native Wayland, renders conversation/attachments, and exits after capture.
The sandbox is stopped. The existing nonfatal portal registration warning remains.

Live installation recheck on 2026-10-03: omachat/omachat-debug are still 0.2.2-1;
omachat.service is active and the daemon is joined to voice. The Arch post_upgrade
hook restarts running per-user daemons, so installation would disconnect that
session. Do not install while it is active without explicit timing authorization.

## Installed local checkpoint — 2026-10-04

The user explicitly authorized installation now, accepting the previously
explained voice-disconnection effect. Both retained main/debug package checksums
match checkpoint.json. Pacman reinstalls the verified local 0.2.2-1 packages.
All 115 regular installed files and package symlinks match the extracted artifacts;
host pacman -Qkk reports 39/39 main and 158/158 debug paths with zero alterations.
Sandbox ownership warnings were artifacts of the sandbox UID mapping; the host
checks are clean.

The user daemon was stopped when checked after installation. It is started and
now active/running, with a connected server state. The updated /usr/bin/omachat
is open under native Wayland, mapped and visible. Voice is disconnected; no voice
channel is rejoined automatically. The known nonfatal portal registration warning
remains. No account/content changes, hosted deployment, commit, push, merge,
release or AUR update occurred.

## Next session: parallel execution — explicit user direction, 2026-10-04

The user requests subagents next session to complete as much remaining work as
possible in parallel, with efficiency as the priority. This explicitly authorizes
delegation for the continuation. Do not stop at another planning pass: implement
bounded independent slices, validate them, integrate, then give freed agents the
next ready task. Desktop remains the first priority; other roadmaps can advance
where they do not delay integration or require unavailable hardware.

### Start and ownership

1. Read this handoff, the current UI plan, channel customization plan, security
   roadmap and latest Android handoff sections. Recheck git status/branches,
   existing uncommitted work, installed state and available tools/hardware.
   Older dated sections record history; the latest October 7 installed state and standing
   installation/publication workflow supersede earlier session-specific limits. Do not repeat accepted desktop voice/
   streaming baseline tests without a change that makes them relevant.
2. Use the available agent capacity: currently four total slots, so the parent
   coordinator plus three workers. Recheck next session; avoid nested spawning
   that oversubscribes the limit. Launch independent work together and keep the
   parent working on integration, scope decisions and verification infrastructure.
3. Give each worker a concrete outcome, exact file ownership and meaningful
   acceptance checks. Tell every worker they are not alone in the codebase;
   preserve others' edits and accommodate concurrent changes. Shared files have
   one writer. A worker must request coordination before editing outside its scope.
4. Parent owns docs, vault updates, CMake wiring, tests/gui/test_operations.cpp,
   package/checkpoint manifests and shared build/integration sequencing. Workers
   use distinct new test files; the parent wires them into the build. Assign
   implementation-specific tests with their owning worker. Keep agent summaries
   short: changed files, evidence, blockers, remaining risks.

### First wave: three independent implementation lanes

| Lane | Initial ownership | Outcome and acceptance |
|---|---|---|
| Theme and shared controls | client/src/platform/ThemeProvider.*, client/qml/components/ shared controls except ArtworkBanner.qml and EmojiPicker.qml; dedicated contrast tests | Audit representative installed Omarchy dark/light palettes, text, selected/hover/disabled states and focus cues. Fix measured failures while retaining theme identity and reduced motion. Normal text target 4.5:1; meaningful non-text/focus cues 3:1. Distinguish decorative/inactive states from actionable controls rather than forcing one threshold onto everything. |
| Desktop accessibility and polish | client/qml/views/, client/qml/dialogs/, EmojiPicker.qml; dedicated interaction tests | Finish keyboard/action/focus traversal, long-name/read-only/offline/pending-operation coverage, density and motion refinements. Check actual minimum/wide sizes at 100%/150%, dark/light. Coordinate token requests with the theme owner. Use accessibility tooling where available; report human acceptance separately from automation. Refresh product screenshots only after final integration. |
| Channel artwork cache foundation | AttachmentActions.cpp, ArtworkBanner.qml, and narrowly identified daemon/cache files after mapping ownership; dedicated cache tests | Implement bounded disk artwork caching and safe invalidation/eviction, preserving account/endpoint/authorization boundaries and active files. Inspect current behavior first so implemented functionality is not duplicated. Exercise replacement, corrupt images, offline loading, account switches/lost access and cache limits. Avoid unrelated controller or wire/schema changes. |

The coordinator confirms exact cache-file ownership before that worker edits.
Changes to AppController.hpp/.cpp or other shared controller files go through one
coordinator-assigned writer. If a lane is blocked, switch that worker to a ready
independent task instead of keeping it idle.

### Refill freed slots with the next ready slices

- Channel Phase 1: bounded crop/focal-point UI and image variants; keyboard/pointer
  move/reorder and safe nonempty-category deletion; combined effective-permission
  view/review; atomic restricted-channel duplication and conflict-aware edits.
  Select the smallest useful slice whose server/daemon/client files can have one
  owner, with permission and compatibility tests. Do not implement all of Phase 1
  as one sprawling change. Notification/favorites/pins/voice-room/admin features
  stay queued behind their prerequisite contracts.
- Restart-persistent desktop failed-send recovery is now a candidate under the
  user's request to advance the remaining plans. Scope identity, encryption,
  retention/deletion and uncertain-delivery semantics before implementation;
  preserve newer drafts and existing safe-retry rules. Avoid storing sensitive
  message content or file paths unencrypted, and do not turn uncertain delivery
  into automatic resend. Assign one controller/storage owner for this slice.
- Android can run independently in a separate worktree/checkout after inspecting
  feature/android-client and preserving its dirty work. Never switch the shared
  desktop checkout out from under workers or assume another branch's uncommitted
  changes exist in a clean worktree. First address voice route/interruption UX,
  denied/revoked permission handling and emulator-testable lifecycle hardening;
  then persisted resumable transfers and remaining mobile features. Physical
  audio/Bluetooth/calls/screen-off/handover acceptance requires actual hardware
  and must remain explicitly unverified when unavailable. Do not clear phone data
  or run live-account actions as test fixtures.
- Security can advance as an isolated research/prototype lane: evaluate maintained
  MLS libraries and integration/packaging contracts, test vectors and migration
  design, then an isolated proof of concept. MLS forward secrecy and SFrame media
  encryption need coordinated protocol/device-state/epoch/downgrade handling and
  review before product integration. Do not bolt custom crypto into production
  merely to fill a parallel slot. No new E2E/forward-secrecy claim until evidence
  supports it. Per-app capture/stereo are separate coordinated media slices.

### Efficient verification and session exit

- Workers run focused checks for their own changes. Parent runs integrated build,
  both QML lint targets and the relevant/full tests at an integration checkpoint;
  do not run identical full suites independently in every agent.
- Give concurrent tests unique sockets, ports, accounts, temporary roots and build
  directories. Serialize native GUI capture, the shared emulator, hardware/audio
  access, screenshots and package installation. Only one writer/build driver uses
  any given build directory. Prefer bounded compiler parallelism (the current
  native runs use -j6); respect the system profile and live resource state.
- Keep working through ready tasks while other agents run. Integrate often and
  replace stale plan status with actual results. Record failures and limits
  honestly; do not count source inspection as behavioral or physical acceptance.
- Preserve session-ses_f0f5.md and all prior edits. Standing October 5 direction:
  always install client-side changes locally for testing; server changes also
  commit/push to the deployment branch and verify the deployed image. Recheck
  live voice/transfers before installation and report any restart interruption.
  Versioned releases and AUR publication require separate authorization.
- Finish with changed scopes, passed checks, installed versus source-only state,
  remaining dependency/hardware gates, and a refreshed handoff plus shared vault.

The full desktop UI plan is `docs/ui-ux-improvement-plan.md`; additional queues
are `docs/channel-customization-plan.md`, `docs/security-roadmap.md` and
`docs/android-session-handoff.md`. The latest local installation checkpoint is recorded in
`build/desktop-permission-sources/evidence/installation-checkpoint.json`; the exact
uncommitted source snapshot is `install/source-manifest.json` in that directory.
The hosted server now runs the deployed permission contract from `16e3ed8`;
see the latest deployment checkpoint and recheck volatile state before acting.
