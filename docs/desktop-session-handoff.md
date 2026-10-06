# OmaChat desktop session handoff

Updated: 2026-10-06. Resume desktop development from this file.

## Current direction

Desktop work is the active priority. **The October 5 source continuation adds
server-resolved exact channel placement, current-user effective-permission review,
and bounded pre-upload artwork cropping.** The three lanes were assigned to
subagents with separate ownership; the parent completed integration and review
when the worker runs stopped due to workspace credits.

Standing workflow authorized by the user on October 5: always install client-side
changes locally for testing. When changes affect the server, also commit and push
to the deployment branch so the server redeploys. This supersedes earlier
session-specific installation and no-push limits. Review and package validation
still apply; public versioned releases and AUR publication remain separate.

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

Remaining ready slices: pointer drag-and-drop, permission-source/other-member and
creation previews, atomic restricted-channel duplication/conflict-aware detail
edits, persisted focal/variant metadata, and the scoped encrypted send recovery.
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
- Installed implementation/source checkpoint: `97f3b43`, pushed to both desktop
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
  `0.2.2-1` checkpoint `97f3b43`, installed October 6 under standing authorization.
  Installed files match the checkpoint packages; both host package integrity
  checks report zero altered files. Hosted server `1aebabf` is verified running/healthy with placement capability.
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
   Older dated sections record history; the October 6 installed state and standing
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
`docs/android-session-handoff.md`. The latest local installation/checkpoint is
recorded in build/desktop-install-647fc1e/checkpoint.json (implementation97f3b43,
packaging647fc1e).
