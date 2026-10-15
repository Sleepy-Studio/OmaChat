# OmaChat feature expansion handoff

Updated: 2026-10-15. Full user-capability audit + expansion backlog.
Resume from this file. Source of truth for behavior is the working tree;
roadmap docs remain the scope record.



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

## Shared framing continuation — local install, rollout pending (2026-10-13)

Opt-in retained sources, immutable source-pixel framing, server-generated display
variants and keyboard Reframe are implemented and locally installed. Crop-only
selection remains the default; source downloads require management permission.
Schema 15 preserves artwork through upload-carrier/final-channel deletion and
checks migration integrity. Release/package **440 each**, native **27 each**, both
QML lints and independent review pass. All **127 installed/runtime artifacts**
match, zero pacman alterations; connected/visible. Backend **83441d0** is pushed
and its matching Docker image published. All five exact-SHA CI/Android/Docker workflows pass.

**Hosted rollout is pending deploy access.** The live endpoint lacks the new
capability; health is good. No deploy tool/browser is available, and the local
MCP credential cannot authenticate to the deploy API. Resume the latest section
of [desktop-session-handoff.md](desktop-session-handoff.md). Later phases and
human acceptance remain open; client edits/transcript are preserved.

## Device-local crop memory installed — 2026-10-13

Approved crop choices restore for the same account/endpoint/server, canonical
image pixels and icon/banner slot after restart. Reset/Cancel and rejected crops
retain the accepted preference; late preparation and epoch renewal retain newer
local adjustments. Persistence is private, off-thread, atomic and bounded to
128 choices/64 KiB/90 days; no source paths or image bytes are stored. I/O/lock/
busy failure falls back to ordinary crop preparation without failing an upload.

Release and clean Arch package each pass 424/424; native/package-native each
21/21 and both QML lint targets pass. Compact/wide dark/light captures are
inspected. All 127 installed artifacts match with zero pacman alterations,
and running client/daemon binaries match; connected/visible. Backend/protocol
is unchanged. The uncommitted source, prior edits and transcript are preserved.
See [desktop-session-handoff.md](desktop-session-handoff.md) and
`build/desktop-crop-memory/` for provenance and limits.

Shared focal metadata and server-generated variants remain **open**. This
precursor remembers crop travel on this device; it retains no originals and
sends the canonical cropped raster through the existing upload contract. Later
roadmap phases and human usability acceptance remain separate.


## Verified desktop continuation — 2026-10-13

HEAD 5708472 includes 928983a plus the structural snapshot-refresh correction.
The role/creation-preview and whole-list placement pass is now installed and
recorded in [desktop-session-handoff.md](desktop-session-handoff.md):
Release/package 414 checks each, native 25 each, all 127 artifacts match with zero
alterations. The backend is pushed/deployed healthy; five exact-SHA workflows
pass. Client changes remain uncommitted. This supersedes the audit baseline's
unrecorded/validation-pending inference; the expansion backlog below is preserved.

## Audit baseline before the verified continuation

- HEAD: `928983a feat(permissions): preview named role and creation access with placement revisions` (2026-10-12).
  - 15 files, +813/-65: `protocol/network.proto` (permission preview fields), `daemon` (`DaemonMethods.cpp`, `ClientState.cpp`), `server` (`AuthHandlers`, `ChatServer`, `ModerationHandlers`, `ServerHandlers`, `State`, `Permissions`), `tests` (`test_permission_preview.cpp`, `test_placement_revision.cpp`, `test_permissions.cpp` unit).
  - This commit directly addresses prior handoff leftovers: named role contributors, role-only/full creation previews, placement revisions. This historical audit observation is superseded by the verified continuation above.
- Prior deployed baseline: `5cf7553` + `bc47c15` (atomic duplication, checked artwork/placement, encrypted send recovery), deployed healthy via Coolify; client installed from 430-file snapshot. See the October 12 parallel-continuation section in `docs/desktop-session-handoff.md`.
- Dirty tree on top of `928983a` (~4800 insertions): `client/qml` (ArtworkCrop, ChannelDetails, ChannelPermissions, ChannelPlacement, ChannelSettings, CreateChannel, QuickSwitcher, BottomBar, ChannelSidebar, MainView), `client/src/controllers` (AppActions +435, AttachmentActions +336, RoleActions +200, AppController), `client/src/platform/ArtworkCache.cpp` (+492), `server/src/core/ServerHandlers.cpp`, plus 12 new untracked GUI tests (`test_artwork_conflicts`, `test_artwork_identity`, `test_channel_details_view`, `test_channel_duplication`, `test_channel_edge_scroll`, `test_channel_settings_navigation`, `test_creation_crop`, `test_permission_sources`, `test_permission_targets`, `test_placement_conflicts`, `test_send_recovery_view`) and untracked `DuplicateChannelDialog.qml`.
  - Inference: active expansion work in progress, not yet validated/recorded. Do not claim it as passing.
- Docs already updated in dirty tree: `docs/channel-customization-plan.md`, `docs/ui-ux-improvement-plan.md`, `docs/desktop-session-handoff.md` (+545). Vault Git commit, public release, AUR publication: none claimed.

## Part A — what users can do today (audited from `client/qml` + controllers + daemon table)

### Messaging
- View history with header/topic/banner, reply/edit state, jump-to-latest (`ChatPane`).
- Send via visible Send/Save action, Enter/Shift+Enter, edit own, reply, attach/paste up to 10 files, `@user/#channel//command/:emoji:` completion (`Composer`, `AppController::sendComposer` -> `message.send`).
- React, reply, edit, copy text/ID, delete, see edited/encrypted state (`MessageDelegate`).
- In-chat image/video thumbnail, external open, save to Downloads; inline audio/video playback with volume/mute, MPV for video (`AttachmentView`, `MediaPlayer`, `attachment.download`).
- Unicode + custom server emoji picker with Recent (`EmojiPicker`, `emoji.create/delete`).
- Full-text search current channel or whole server, jump to match, return-to-latest (`SearchPanel`, `message.search`).
- Typing indicator, read markers on visible focused newest content, history paging (`typing`, `message.read`, `message.history`).
- Failed-send recovery: retain text/files/reply, safe retry after explicit rejection only, timeout asks to check delivery + Copy/Dismiss, Review send from StatusBanner; restart-persistent encrypted recovery in `bc47c15` slice (20 records/128KiB/30d, keyring-bound, never auto-resend uncertain).

### Channels / servers / DMs
- Server rail: Home vs servers, unread/mention/voice badges, add/create/join, instance console entry (`ServerRail`).
- Sidebar: browse/collapse categories, join text/voice, live participants/volume/speaking, drag reorder with threshold + edge scroll, right-click create/settings/permissions/duplicate/leave/invite, group DM create/add/rename (`ChannelSidebar`, `channel.update` placement fields).
- Create Text/Voice/Category with name/topic/description/icon/banner + inherited-access review (`CreateChannelDialog`).
- Duplicate channel/category config under new name (type, topic, desc, artwork refs, category, overrides; never messages/members) (`DuplicateChannelDialog`, `channel.duplicate`).
- Exact placement dialog: destination category + visible sibling/end, atomic server-resolved ordering incl. hidden siblings (`ChannelPlacementDialog`).
- Settings: Overview/Appearance/Permissions sections, draft retention, CAS saves with conflict retain + Reload latest (`ChannelSettingsDialog`, `channel.details.cas.v1`).
- Read-only details view with banner reserve, loading/retry/fallback (`ChannelDetailsDialog`).
- Server create (+Discord JSON import), join via invite, leave/delete, name/desc/icon/banner edit with crop review (`CreateServerDialog`, `ServerSettingsDialog`, `server.*`).
- Mute per channel (boolean only), collapse per user, QuickSwitcher Ctrl+K fuzzy jump, member list with profile/bio/DM/mention/moderate entry.
- Multi-account: several connected, one active; account switcher; background notify/count only.
- CLI parity: `channel place/create`, `dm`, `group`, `message history --before`, `role/override`, `invite`, `moderation kick/ban`, `stream`, `e2e`, `presence`.

### Voice / media
- Join/leave voice, mute/deafen, PTT vs VAD with threshold, per-user volume, presence Online/Idle/DND (`BottomBar`, `voice.*`, `audio.*`).
- Screen share via portal + PipeWire, H.264 NVENC/AMF/x264, keyframe-on-demand, shared-memory frames, watch tabbed + own preview/PiP, per-stream volume, pop-out with zoom/pan, stop watching (`StreamPanel`, `StreamWindow`, `stream.*`). Sound opt-in per share (captures all other app playback, not per-window).
- Attachments share control connection (512 KiB chunks, 4 in flight); uploads resume via `ResumeUpload`; transfers survive reconnect, listed via `transfer.list`, cancellable.

### Settings / appearance
- Appearance: interface scale 75-300%, reduced motion, persistent atomic save (`config.set_ui`).
- Voice & Audio: mic/speaker select, VAD/PTT, threshold, bitrate; Screen sharing opts; Notifications toggles (messages/mentions/voice_join only).
- Account: display name/avatar URL/bio, OAuth link (Discord/GitHub; Google hidden), delete account, sign out (`profile.update`, `account.oauth*`).
- Artwork crop review: drag/arrows/zoom/Reset, bounded decode, canonical PNG output 512x512 icon / 1024x256 banner, fingerprint-bound to account/server/kind (`ArtworkCropDialog`).

### Admin / moderation / security
- Roles: create/rename/recolor/reorder/delete, permission toggles, member assign chips (`ServerSettingsDialog`, `role.*`).
- Channel overrides: per-role/member Allow/Inherit/Deny, effective-permission review for self (+ `928983a`: named role + creation + placement revisions — verify before claiming), manager preview of existing members (`ChannelPermissionsDialog`, `permission.preview`).
- Moderation: kick/ban/unban, voice mute; instance operator console (live counts, suspend/restore, cross-community kick/ban, community delete, audit, registration toggle, supervised restart) (`instance.*`, schema v12).
- E2E: DMs + 3-10 groups, text + files, device keys, safety numbers + verify, key-change warnings, fail-closed no-plaintext-downgrade (`e2e.safety/verify`). Server channels explicitly NOT E2E by design.

## Part B — what we can add (grouped, smallest useful slice first)

### P0 — original Phase 1 checklist (implementation complete; live acceptance pending)

Items 1–4 below describe the original checklist and its historical base. The
current source baseline is `87822ff` plus the keyring fix. Placement revisions,
conflict UX and shared framing/variants are implemented and covered by the
recorded package/native checks; do not rebuild them as unfinished features.
Use the current resume point above for installation and live acceptance.

1. **Verify `928983a` + dirty tree.** Run warnings-as-errors Release, full CTest, both QML lints, native Wayland permission/placement checks, fresh Arch packages + install checkpoint, exact-SHA CI/Android/Docker, Coolify rollout if server touched. Record in handoff. Do not stack new protocol on unverified base.
2. **Persisted focal-point metadata + image variants.** Store focal x/y per icon/banner; server generates bounded variants; client selects variant by slot. Acceptance: crop survives re-upload, old clients fall back to full asset, no metadata leak.
3. **Whole-list placement revision + hidden-sibling conflicts.** Server list revision; placement CAS compares revision, explains hidden reorder. Acceptance: two-manager race shows who moved what, retains draft, no silent overwrite.
4. **Artwork/placement conflict UX (dirty tests suggest in-flight).** Finish `test_artwork_conflicts` / `test_placement_conflicts` / `test_artwork_identity` coverage; wire to dialogs. Acceptance: conflict retains local input, Reload latest explicit, no auto-overwrite.

### P1 — personal organization + notifications (Phase 2, all proposal)
- Notification levels (All / mentions-only / none) + `@everyone`/role split; replace mute boolean with migration.
- Mute duration (1h/8h/24h/until-changed) with expiry display; survives restart/reconnect.
- Favorites section (personal order, no server effect).
- Hide channel (discoverable via manage/search, notifications still apply); persist collapse per user.
- Unread controls: mark channel/category read, jump-to-first-unread, unread-only filter; markers survive reconnect, per-account.
- Activity density: show/hide voice participants, compact sidebar toggle.
- Decision needed before UI: local daemon DB vs synced account-preferences protocol. Recommend: ship local first with explicit "device-local" copy, then sync protocol.

### P2 — text channel behavior (Phase 3, all proposal)
- Pinned messages (bounded per-channel limit, pin list, tombstone/deleted handling, live sync).
- Slow mode (server-enforced interval, edits/mods exempt, composer countdown, reconnect-safe, no dual-client bypass).
- Posting policy (Send/Attach/links/embeds/reactions/thread-create; GUI disables + server rejects).
- Channel guide (structured checklist on first visit, reopenable).
- Archived/read-only with reason + reopen; search/history preserved, sends fail server-side.
- Announcement channel (publisher-only post, follow/notify, styling; no crosspost v1).
- Retention policy (indefinite/fixed window, preview affected history, consistent attachments/search/replies/audit handling).
- Threads then Forum as separate releases (branchable subscribable threads with own unread/archive; forum post list/tags/sort/filter on top). Do not bundle with basic customization.

### P3 — voice channel behavior (Phase 4, all proposal; after two-machine baseline)
- User limit with visible full state; bypass only if explicitly defined.
- Quality preset (target bitrate within server limits, relay announces actual).
- Stream cap (resolution/fps/concurrent streams, subordinate to server policy).
- Join/speak presets (open / listen-only / moderator-led via existing permissions; modified client cannot bypass).
- Temporary status text (expiry/clear rules, live sync; distinct from topic).
- Temporary voice rooms (template-created, idle expiry with grace, never delete occupied, restart-safe).
- Voice lobby (waiting area + explicit admission; no audio/video pre-admission).

### P4 — power administration (Phase 5; order: preview -> templates/bulk; audit before retention/large edits)
- View-as role/member (View/History/Send/Connect/Speak/Stream/Manage with category+channel sources; matrix-tested vs server decisions).
- Category permission sync (copy-to-children or inherit; explicit preview of every changed channel, cancellable; never silent replace).
- Templates (save type+settings+role overrides; member-override opt-in; atomic apply, no restricted-channel leak).
- Audit log (who/when/old/new for settings/perm/move/pin/archive/retention; owner-controlled access/retention; every accepted mutation has one entry).
- Bulk edit (multi-select move/archive/template with diff preview; per-channel partial-failure report or documented rollback).
- Import/export config (tree + settings, no messages/secrets; ID mapping + collision preview; no silent duplicates).

### P5 — visual follow-ons (small, high delight-per-cost)
- Channel accent (server-shared, restrained; readable text derivation; local "use theme colors" override; contrast-gated).
- Emoji icon (Unicode/server emoji as light icon; fallback to type icon on deletion).
- Welcome card (banner + desc + links + pinned rules; first-visit, dismissable/reopenable, never forced).
- Channel links (curated labeled HTTPS list via external-link flow; scheme/label validation).
- Personal chat background (account/device-local tint/static image + opacity; high-contrast suppresses; never uploaded).
- Banner visibility (personal compact/expanded/hidden; compact keeps composer + latest visible at 720x460).

### P6 — security / media (explicit gates; never label early)
- MLS RFC 9420 for DMs/groups: library select+pin+vectors; server key-packages/Welcome/proposals/commits (never holds secret); bind to device keys/safety numbers; crash-safe epoch persist; per-conversation migration with send-block during epoch change; full compromise/removal/late-join/reinstall/replay/tamper/rollback/mixed-version tests + review before default-on. Old messages keep original protection.
- SFrame RFC 9605 for Opus/H.264: keys from MLS epoch; hop-by-hop transport auth retained; no "E2E" label until relay provably cannot decode captures; sender key/counter uniqueness, no reuse across restart, rotation on join/leave/compromise, bounded replay, content-blind keyframe requests, <1400B fragmentation budget; loss/reorder/fragment/reconnect/2-device/malicious-relay/late-subscriber/membership-during-share tests.
- Audio capture: stereo path + mono fallback with versioned marker; separate explicit audio-source picker + live indicator (cannot infer from portal window choice). DMA-BUF/VAAPI/QSV after privacy + two-machine baseline measured.

### UX / acceptance debt (applies to every slice)
- Independent human acceptance distinct from automated checks (5 baseline tasks: join channel, send/reply, find old message, join/leave voice, create channel; pointer-only + keyboard-only; min 720x460/150% + normal 1440x900; dark/light).
- Custom role colors + artwork overlays contrast audit; selected/disabled states across all 22 palettes; focus/target sizes at 1x and 1.5x.
- Release gate per slice: QML lint, build, relevant tests, live sandbox incl. offline/reconnect, read-only channel, long names, pending uploads, screen share, reduced motion.

## Recommended execution order

1. Installation and live artwork acceptance are complete; use the latest checkpoint above.
2. Record completed Phase 1 implementation and remaining human acceptance separately.
3. P1 notifications/mute-duration + favorites/hide/unread (highest user value, mostly client/daemon-local).
4. P2 pins + slow mode + archive (server-backed; each needs migration + capability + CLI + tests).
5. P5 visual follow-ons interleaved as small wins between heavy slices.
6. P4 admin (preview -> sync -> templates -> audit -> bulk -> import/export).
7. P3 voice controls after two-machine voice/screen baseline measured.
8. P6 MLS/SFrame last, with independent review; no UI label changes until gates pass.

## Next session start here

1. Read the **Phase 2 plan completed** checkpoint and
   [implementation plan](phase2-personal-preferences-plan.md), then inspect
   `git status` and `git log --oneline -5`. Expected source: `87822ff` plus the
   installed local keyring fix; preserve all edits and ignored evidence.
2. Implement slice 2A storage migration and pure notification policy tests,
   followed by daemon/IPC, CLI and GUI. Phase 1 installation/live artwork gates
   are complete. Phase 2 application implementation has not started.
3. Client changes need package/runtime parity and native acceptance. Recheck
   voice/sharing before disruptive work. Server changes follow the standing
   main-push/verified-rollout workflow. No hosted deploy is needed for 2A.
4. Update both handoffs and the shared vault with actual results and open gates.
