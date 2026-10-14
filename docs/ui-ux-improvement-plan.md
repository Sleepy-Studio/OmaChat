# OmaChat UI/UX improvement plan


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


## Installed role previews and whole-list placement — 2026-10-13

Named winning role contributors, manager role-only previews and authoritative
inherited creation previews are installed. Ordinary self-review retains generic
sources; role-only means the default role plus one role, without a real member
or personal override. Whole-server opaque placement tokens cover hidden siblings
and structural ABA/restart, with full authorized snapshot refresh and retained
choices on conflicts. Drag keeps its press-time original. Reload waits for the
matching current snapshot and never reinstalls stale reply metadata.

Release/package each pass 414/414, native/package-native each 25/25, both QML lint
targets and format checks pass. Compact/wide dark/light captures confirm loaded
rows, plain names, readable type labels and fixed actions. All 127 installed
artifacts match with zero alterations; client connected/visible. Backend 5708472
is pushed and deployed healthy after five exact-SHA workflows and matching image
publication. Exact provenance and bounded guarantees are in
[desktop-session-handoff.md](desktop-session-handoff.md).

Remaining: persisted focal metadata/artwork variants, later roadmap phases and
human usability acceptance. The feature expansion audit remains a separate
backlog; this section supersedes older remaining-role/placement queues below.


## Parallel desktop continuation — 2026-10-12

Implemented in the working tree: atomic configuration duplication with copied
permission overrides and shared artwork references; checked artwork/placement
saves with retained draft/conflict reload; encrypted restart-persistent send
recovery. Category snapshots and events honor ViewChannel; a visible child whose
parent is hidden remains reachable at top level. Shared artwork authorization
uses any visible referencing channel and survives source deletion.

Artwork CAS compares original icon/banner references. Placement CAS compares
source parent/position, while before-target validity is checked against current
server state. The placement dialog also detects visible source/destination/order
changes and retains the selected destination until explicit reload. This does
not compare a whole server list revision or detect every hidden-sibling reorder.
Duplicate categories copy their configuration, not children. The GUI keeps the
source category and blocks repeat submissions after uncertain completion.

Encrypted recovery uses an independent secure keyring key, authenticated
ciphertext and account/endpoint/verified-certificate/user binding. Limits are
20 records/128 KiB/30 days per account. Uncertain sends never auto-resend; only
explicit server rejection permits a deliberate retry. Storage, acknowledgement
cleanup and purge failures remain visible. Source/installed/deployed state and
validation evidence are in [desktop-session-handoff.md](desktop-session-handoff.md).

Remaining: named role contributors, role-only/full creation previews, persisted
focal metadata/variants, whole-list placement revision checks and later roadmap
phases. Human usability acceptance remains separate from automated checks.


## Permission sources and member previews — 2026-10-11

Installed: server-authoritative resolution stages for current-user access and
manager previews of existing members. Numeric self IDs, pending/snapshot refresh,
deleted contexts and stale replies are guarded; older servers retain self mask
review. Named role contributors, role-only and creation previews remain open.
Release/package 337/337, native/package-native 10/10, both QML lint targets and
independent review pass. All 126 installed artifacts match; zero alterations,
connected/visible. Backend 16e3ed8 is pushed/deployed; exact-SHA CI/Android/Docker
pass, live capability and 21-row mask parity verified. Client work remains in the
uncommitted installed snapshot. See desktop handoff and
`build/desktop-permission-sources/` for provenance and limits.

Next pass explicitly requires coordinator + three parallel subagents to complete
as much ready work as possible, with exclusive file ownership, worker refills,
central integration and serialized installation/deployment/native checks.

Last reviewed: 2026-10-11. Conversation-flow improvements shipped in v0.2.2. Desktop appearance, persistent operation feedback and administration safeguards are implemented; minimum-size dark/light conversation acceptance and artwork loading stability pass; compact administration and drawer/focus acceptance also pass.

## Channel settings and room details — 2026-10-07

Installed locally: Overview/Appearance/Permissions navigation retains drafts and
CAS/discard behavior; keyboard focus reveals clipped artwork actions. The current
account's authoritative access opens the existing overrides editor without losing
the underlying draft. Offline/read-only management is disabled. Joined voice has
a details action that preserves the selected conversation and voice session.
Shared room details reserve banner geometry, show retry/fallback, scroll long
plain topic/safe description and clear on context/access loss.

Independent review, warnings-as-errors Release **304/304**, both QML lint targets,
formatting and **11/11 native Wayland checks** pass. Inspected dark/light compact
720×460/150% and wide 1440×900/100% captures include focused artwork action bounds.
Installed main/debug artifacts match all 126 paths with zero alterations; client
and daemon are connected/visible. Hosted connectivity was restored by allowing
Mullvad LAN traffic and restarting the unhealthy application at existing 891e4c2.
See the desktop handoff and `build/desktop-room-settings/` for exact provenance.
Full source/member previews, artwork conflicts/variants and durable encrypted
recovery remain open; this is automated acceptance, not a public release.

## Creation inherited-access continuation — 2026-10-07

The installed creation form offers a current-user inherited-access preview from
server/category masks, filtered for text/voice/category. It refreshes live, clears
rows offline and focuses the viewport when opened for keyboard scrolling. Create
and Cancel remain fixed at compact 720×460/150% size. Full permission-source and
other-member creation review remain open.

Independent review, warnings-as-errors Release packaging, final **293/293 CTest**,
both QML lint targets and **6/6 package-built Wayland checks** pass. Dark/light
capture assertions verify the actual palette; installed files match all 126
artifacts with zero alterations, daemon connected and client visible. Source is
uncommitted based on d35634a; no server change or publication. Exact provenance,
focus regression and remaining work are in [desktop-session-handoff.md](desktop-session-handoff.md).
Evidence: `build/desktop-access-review/`.

## Client continuation — 2026-10-07

Initial channel artwork uses crop approval and private staged ownership, with
retained crops after rejected creation and explicit partial-success handling
that prevents duplicate channels. Dirty creation drafts have discard protection
and crop closure restores form focus. Pointer placement gains bounded edge
scrolling with source-grab retention. Quick-switcher and joined-voice identity
surfaces show channel icons with fixed fallbacks; compact labels and the result
viewport remain bounded. Native keyboard checks distinguish internal focus from
active compositor-window focus.

The reviewed continuation is installed locally; final package 291/291 CTest,
both QML lint targets and 13 native Wayland checks pass. All 126 installed
artifacts match with zero package alterations. Source remains uncommitted.
Verification/installation provenance and remaining work are recorded in the
[desktop handoff](desktop-session-handoff.md). The implementation is client-only;
the hosted server remains on the October 6 contract. Permission-source/member
previews, full creation access review and restart-persistent encrypted recovery
remain open. Automated acceptance remains distinct from human acceptance.

## Installed and deployed checkpoint — 2026-10-06

Reviewed implementation `97f3b43` is installed locally and pushed to `main` and
`feature/desktop-accessibility`; 257/257 CTest, warnings-as-errors, QML lint,
formatting and native pending-invalidation regressions pass. Packaging fix
`647fc1e` declares WebP's Qt image-format dependency; refreshed main/debug
packages are installed, byte-identical binaries verified, and 126 artifacts
match with zero altered paths. Server image correction `1aebabf` is published
and deployed healthy; the connected client sees `channel.placement.v1`.
Final native, Android and Docker CI pass on `aee3e7e`. This supersedes historical
source-only/uninstalled status below. Evidence and remaining work are in the
[desktop handoff](desktop-session-handoff.md).

Standing user workflow: always install client changes locally for testing;
server changes also commit/push to the deployment branch and verify rollout.

## Resume and user acceptance

Resume from [desktop-session-handoff.md](desktop-session-handoff.md).
On 2026-10-02 the user confirmed desktop voice and streaming work and marked that
acceptance checkpoint complete. Persistent upload/failed-send/reconnect feedback
is now implemented in the working tree. Channel creation and unsaved-settings protection are also implemented. Minimum-size dark/light conversation, keyboard search and recovery acceptance now pass.
The refreshed local desktop package is installed October 4. The subsequent source continuation adds measured theme/shared-control states, keyboard/layout polish, scoped bounded artwork caching and conservative safe category deletion; see the handoff for verification. Previous implementation commit `92ec294` has green
native and Android GitHub CI; new source has local verification only.

The theme audit covers 22 locally installed palettes, portable representative
snapshots and adversarial selections: normal/action text 4.5:1 and meaningful
focus/input cues 3:1. Disabled controls are intentionally dimmed. Role-custom
colors, artwork overlays and independent human acceptance remain separate.
Future restart-persistent send recovery is scoped in
[desktop-send-recovery-design.md](desktop-send-recovery-design.md), not implemented.

The parallel source continuation passes clean Release configure/build,
**238/238 CTest**, both QML lint targets (existing warnings), and native Wayland
keyboard/conversation/search/sidebar acceptance. The dark/light compact/wide
matrix passes at 100%/150%, with one-pixel compositor rounding permitted for the
wide size. All 22 installed palettes pass the native shared-control audit.
Evidence is retained in `build/desktop-continuation-validation/evidence/`.
Implementation commit `f285540` is locally committed and installed from fresh
main/debug packages at explicit user request. The package passes 232 checks plus
6 theme checks; artifact/file integrity is verified. Build provenance and install
logs are retained in `build/desktop-install-f285540/`. No push or hosted deployment.

## Channel foundation continuation — 2026-10-05

Three parallel desktop lanes add exact placement using stable server-resolved
sibling IDs, authoritative current-user effective-permission review, and bounded
pre-upload icon/banner crop controls. New dialogs support keyboard and pointer
input with retained pending/error feedback. This continuation is source-only;
the installed October 4 implementation and hosted server remain unchanged.
See the handoff for final validation and the remaining Phase 1 scopes.

## Next-session execution preference — 2026-10-04

The user explicitly requests parallel subagents to complete as much remaining
work as practical efficiently. Follow the ownership, first-wave assignments,
refill queue and integration gates in
[desktop-session-handoff.md](desktop-session-handoff.md#next-session-parallel-execution--explicit-user-direction-2026-10-04).
Start with theme/shared controls, desktop keyboard/polish and channel artwork
cache work in independent lanes; the parent coordinates integration. Advance
Android and security in isolated later lanes where prerequisites permit.

## Local checkpoint installed — 2026-10-04

The user authorizes installation now. Refreshed main/debug 0.2.2-1 packages are
installed, byte-verified against the retained artifacts and host-integrity-checked
with zero altered paths. The daemon is active and connected; the updated client
is open and visible under native Wayland. Voice is disconnected for the user to
rejoin. No hosted deployment, Git commit/push or public release is performed.

## Compact administration and keyboard acceptance — 2026-10-03

Role settings uses a compact selector and scrolling form with fixed actions.
Focused colors/permissions scroll into view; role selection, colors and member
assignment chips support keyboard activation. Member drawer/profile and composer
emoji flows restore usable focus on closure. Composer/reaction emoji pickers fit
the window; search and Unicode/custom grids support keyboard selection. Compact
custom emoji upload and Delete controls fit the visible panel.

The real production-QML/controller/server/daemon matrix passes at actual
720×460/150% and 1440×900/100% sizes in dark/light, offscreen and native Wayland.
It exercises a second member, assignment/removal, custom emoji deletion,
color/permission editing with Revert, emoji insertion and drawer/profile return.
Captures are inspected. Full 211-test pass preceded final focus/reaction additions;
all 12 operation tests pass afterwards, with both QML lint targets (existing
warnings). Release package checks pass 209/209 plus 2/2 theme tests; the final refreshed
Release GUI suites pass 14/14 and the extracted executable passes a native
Wayland live-sandbox launch. The verified local package is installed October 4
with explicit user authorization.

## Implementation status

- **Source complete for Priority 1:** Compact windows place channels and members in temporary drawers; the bottom bar keeps its key controls visible. The composer has a pointer-accessible Send/Save action and a shortcut hint. Empty/error states have contextual actions. Search shows in-context progress/errors and opens a matching message with earlier history plus a clear return to latest. The first Priority 2 item also has a keyboard path: focus the message list, choose a message with arrow keys, and press Enter for actions.
- **Verified:** Full build, QML lint, and 125 unit tests pass. A live isolated sandbox rendered the real main view at 720×460 and 1200×760 using the development screenshot mode; the compact chat and composer are visible without clipping.
- **Still to validate:** Independent human acceptance, custom role colors and artwork overlays. Theme token/shared-control coverage includes all 22 installed palettes. Compact role/member/emoji and drawer/focus acceptance pass. Desktop voice and streaming are user-confirmed working. The earlier October 4 package remains installed; subsequent source changes and the hosted server are not installed/deployed.

For repeatable visual checks, `omachat --screenshot /tmp/omachat.png` remains the self-contained CI smoke test. Set `OMACHAT_SCREENSHOT_LIVE=1` with `OMACHAT_SOCKET` pointing to an isolated test daemon to capture real chat; `OMACHAT_SCREENSHOT_WIDTH`, `OMACHAT_SCREENSHOT_HEIGHT`, and `OMACHAT_SCREENSHOT_DELAY_MS` control the capture. Do not point this mode at a production account for test fixtures.

## Desktop accessibility continuation — 2026-10-02

Settings now exposes an Appearance tab with live interface scale, reduced motion,
reset, and persistent save/error feedback. Preferences are saved atomically by
`config.set_ui`; invalid requests and failed writes retain the prior state.
Saved app scale now uses the same app metrics at launch and during edits, while
Qt retains the user's desktop display scaling.

This uncovered two existing scale/motion bugs: QML preferred ThemeProvider's
accessible default constructor over its factory, creating a second provider;
and C++ `Theme.px()` did not establish a binding dependency on scale. QML now
uses the instance configured by main(), and the small `Metrics.px()` singleton
reads `Theme.scale` inside each binding. Existing metric calls use that helper.
A regression exercises the real QML factory, startup values, live metric changes,
reduced motion, and engine ownership. Qt's construction rule is documented in
[QML_SINGLETON](https://doc.qt.io/qt-6/qqmlintegration-h.html#QML_SINGLETON).

Small text in Settings, message metadata, and the composer now uses `textMuted`.
That token meets 4.5:1 against the normal background, sidebar, input, and raised
surfaces in the built-in dark palette and the test light palette. A complete
all-theme and selected/disabled-state audit remains open.

Use `OMACHAT_SCREENSHOT_SETTINGS=1` with the existing live-sandbox screenshot
mode to capture the actual Settings dialog. The capture waits for its transition.
Final validation: native build and **199/199 CTest tests** pass, including the
real QML singleton/live-metric/contrast tests and daemon preference validation,
reload, and failed-save rollback. Both QML lint targets finish successfully;
existing warnings elsewhere remain. A temporary native QtTest driver against
an isolated real daemon exercises keyboard scale changes, keyboard reduced
motion, saved preferences, and pointer reset. Actual 1200×760/100% and
720×460/150% dark/light captures are checked; compact Settings uses a section
picker and keeps Reset visible. A Wayland sandbox launch and Settings capture
also succeed (the compositor chooses the final window size; a nonfatal portal
registration warning remains). The full conversation workflow walkthrough remains open. Desktop voice and
streaming acceptance is complete by user confirmation on 2026-10-02.

No installed package or hosted server is changed by this source continuation.

## Persistent operation feedback — 2026-10-02

Composer retains upload progress and terminal outcomes beside the affected
conversation, with Cancel for active transfers and Dismiss for outcomes. A bounded
scrolling area protects the input in narrow layouts. File completion is labelled
separately from message delivery. Active transfers are rediscovered through the
existing daemon `transfer.list`; account-qualified keys prevent cross-account
collisions and stale snapshot reconciliation avoids orphaned progress rows.

Failed sends retain text, file paths, reply and action state without replacing a
newer draft. Safe Retry reuses that operation only after an explicit rejection,
with the original account/channel, current permission and an active connection.
Repeated Retry during a pending send is ignored. Timeout/transport uncertainty
asks the user to check delivery and offers Copy text/Dismiss. StatusBanner keeps a
Review send action available when the user leaves an affected channel.

These recovery records last for the GUI session; disk-backed failed-send recovery
is not implemented. Active transfers remain daemon-owned. The GUI limits retained
send operations to 20 and terminal transfer history to approximately 100 rows.

Native build, **205/205 CTest tests**, both QML lint targets (existing warnings),
and whitespace checks pass. Six new real-daemon/controller tests cover rejection,
retry without duplicate delivery, cancellation, disconnect/reconnect, account and
channel scope, and read-only permissions. Production Composer QML passes pointer
Retry at 150% and keyboard Retry at 100%, offscreen and under native Wayland.
Narrow captures are checked; full-main-window/dark-light/workflow acceptance remains.
Source and docs are uncommitted; no installed package or hosted server changed.

## Administration continuation — 2026-10-02

Channel creation initially shows type, name and category. Optional settings keeps
topic, description, artwork and the accurate access explanation available before
submission. Create waits for acknowledgement; rejected input remains in place.

Server/channel/role saves show persistent results beside their section. Dirty
editors ask on Close/Escape, block outside dismissal, and cannot close while a
save is pending. Role switching asks before discarding a draft. Rejected role
saves retain dirty state; remote updates and save replies preserve newer edits.
Accepted channel/server fields display server-normalized values. Artwork/category
changes still apply immediately, with explanatory text in the editor.

Native build, **208/208 CTest**, both QML lint targets (existing warnings) and
whitespace checks pass. Three production-QML/real-daemon tests cover rejected
input, optional fields, normalized values, remote/newer edits, role switching,
Escape/outside protection and discard decisions. Pointer/Escape flows pass
at 720×460 offscreen (150% creation, 100% settings) and native Wayland; captures
are inspected. Native compositor size differs from the offscreen test window.
Full-main-window/light-theme/conversation/compact-role acceptance remains open.
Source/tests/docs are uncommitted; installed package and hosted server unchanged.

## Conversation and artwork acceptance — 2026-10-02

Server and channel banners reserve geometry while previews download/decode and
retain an unavailable-artwork fallback; failed downloads offer Retry. Category
icons retain a fixed slot with a fallback. Existing server-rail initials and
channel-type fallback icons already preserve their geometry. Decode failures
currently offer the fallback only; the cached file is not forcibly re-downloaded.

Full-window production QML is exercised against the real isolated server/daemon
and controller. Keyboard send/reply, nine-result search, exact-match navigation,
Return to latest, Review send navigation and pointer Dismiss pass at minimum size
(720×460), 150% scale, dark/light palettes, offscreen and native Wayland. Captures
are inspected. Search results now scroll within a window-bounded panel; the
warning banner uses a readable themed surface rather than a dark yellow fill.

Native build, **210/210 CTest**, both QML lint targets (existing warnings), and
whitespace checks pass. Compact role/member/emoji, broader drawer/focus and
all-theme selected/disabled states remain open. A clean Release Arch build passes 208 package checks plus two separately run
clean theme tests. Main/debug packages and verification evidence are preserved in
`build/desktop-checkpoint/` with checksums and uncommitted-source provenance. The
clean executable launches under native Wayland against an isolated real sandbox.
Installation and hosted deployment remain unchanged.

## Product direction

Keep OmaChat a fast, native communications workspace: compact, readable, and keyboard friendly. Use the active Omarchy palette as the visual identity. Spend accent color on selection, focus, speaking, and actions that need attention; keep routine chrome quiet. Preserve the user's place when opening search, settings, and channel details. Add motion only where it explains a state change, using the existing reduced-motion setting.

The existing client already has useful foundations: a virtualized message list, theme and scale support, keyboard shortcuts, focus rings on common buttons, accessible labels in many views, and clear voice status. This plan builds on those foundations rather than replacing the layout or introducing a visual framework.

## Evidence and limits

- Source reviewed: `client/qml/views/`, `client/qml/dialogs/`, `client/qml/pages/`, `client/qml/components/`, `client/qml/Main.qml`, and `ThemeProvider`.
- The repository's `docs/screenshots/main.png` shows a 1200×760 sample. It is useful for overall hierarchy, but newer media and artwork changes may not be represented. Current source was used for behavioral findings.
- No live usability sessions or current multi-size render were run for this planning pass. Visual judgments below need validation against the installed client.

## Priority 1: core conversation flow

| Change | Why it matters | Implementation area | Acceptance check |
|---|---|---|---|
| Make the layout respond to available width. Collapse the member list first, then offer the channel list as a reversible drawer or toggle; keep a useful chat width at the 720×460 minimum. | The rail, channel list, and member list reserve 62 + 236 + 220 scaled pixels before chat, while the member list only auto-hides above/below one width threshold. | `MainView.qml`, `ChannelSidebar.qml`, `MemberList.qml`, `ChatPane.qml` | At minimum size and at 1.5× UI scale, messages, composer, and voice controls remain usable without clipped critical actions; switching panes preserves selection and scroll position. |
| Make sending discoverable without weakening Enter-to-send. Add a visible send action when there is text or an attachment, with the shortcut explained near the composer. | The composer supports Enter and Shift+Enter, but has no visible send control. First-time mouse users have to infer how to send. | `Composer.qml` | A new user can send text and a file with a pointer; Enter and Shift+Enter still work; disabled and sending states are clear. |
| Give search a complete task flow: searching, no results, error, result focus, and navigation to the exact matching message. | Search currently lists results and selects a channel on click; it does not visibly establish that the matching message was reached. | `SearchPanel.qml`, `ChatPane.qml`, message navigation/controller as needed | From a whole-server result, the destination message is visible and highlighted briefly. Keyboard selection works. A failed search offers retry and retains the query. |
| Make empty and failure states actionable. | Empty conversation, no channel, and failed message loading states are mostly descriptive text. | `EmptyState.qml`, `ChatPane.qml`, `ChannelSidebar.qml` | Each state has one relevant action when action is possible: start a conversation, open switcher, send first message, or retry loading. Read-only channels do not imply that posting is available. |

## Priority 2: accessibility and state clarity

| Change | Why it matters | Implementation area | Acceptance check |
|---|---|---|---|
| Give message actions a keyboard path independent of hover. Show the selected message's actions or a context menu on keyboard focus. | `MessageDelegate.qml` reveals its action strip only on hover; the right-click menu is a pointer path. | `MessageDelegate.qml`, `ChatPane.qml` | Tab and arrow-key users can reach a message and reply, react, copy, edit, or delete as permitted. Focus returns predictably after an action. |
| Audit small text and icon contrast across Omarchy themes. Treat `textFaint` as decorative or large-text color, and use a stronger token for 11–12 px timestamps, helper text, and status copy. | `ThemeProvider` targets 3:1 for `textFaint`, while several small text labels use it. | `ThemeProvider`, QML views and dialogs | Normal text reaches 4.5:1, key non-text controls and focus cues reach 3:1, in built-in dark and representative light/dark Omarchy themes. |
| Expose UI scale and reduced motion in Settings, and check focus/target sizes at each scale. | The runtime supports both, but Settings does not expose them; message toolbar buttons can be 28×26 scaled pixels. | `SettingsDialog.qml`, `ThemeProvider`, shared controls | Scale is adjustable in app and survives restart. Reduced motion removes nonessential animation. Pointer targets and keyboard focus remain practical at 1× and 1.5×. |
| Use durable feedback for operations that can outlast a toast: upload, reconnect, send failure, OAuth linking, and screen share. | The global notice is transient, while these actions can need a retry or a clear final state. | `Main.qml`, `Composer.qml`, `StatusBanner.qml`, related dialogs | An operation's outcome remains visible in its context until resolved or dismissed; the user can retry where retry is safe. Screen sharing remains unmistakable when the stream panel is collapsed. |

## Priority 3: complex flows and visual craft

| Change | Why it matters | Implementation area | Acceptance check |
|---|---|---|---|
| Shorten channel creation's first step to type, name, and category. Put topic, description, artwork, and access review behind an optional second step. | The current create dialog presents every field before the first channel exists. | `CreateChannelDialog.qml` | Default channel creation takes only the essential inputs; advanced options remain available before submission; permissions are described accurately. |
| Protect unsaved settings and show save results beside the edited field or section. | Server and channel settings contain editable fields and dirty state, but close and concurrent-update behavior need a focused usability pass. | `ServerSettingsDialog.qml`, `ChannelSettingsDialog.qml`, `SettingsDialog.qml` | Closing a dirty editor asks whether to discard; rejected edits retain input and show the server response; successful saves clearly identify what was saved. |
| Reserve image/banner space while assets load and supply a clear fallback. | Banners are shown only when `Image.Ready`, which can shift surrounding content after a download. | `ChatPane.qml`, `ChannelSidebar.qml`, `ServerRail.qml` | Loading artwork does not move the composer or selected navigation; failed artwork keeps an intentional fallback and can retry. |
| Refine density and motion after task flows pass. Use consistent type sizes, spacing, hover/focus treatment, and short directional transitions for drawers and contextual panels. | The current screen is coherent but some content and controls are visually quiet; motion should clarify where panels came from. | Shared QML controls and views | One visual language across dark/light themes; no decorative gradients or new heavy dependencies; reduced-motion path verified. |

## Delivery order

1. **Baseline:** Capture the current installed client at 1200×760, 720×460, and 1.5× scale in built-in and Omarchy light/dark themes. Walk five tasks: join a channel, send/reply, find an old message, join/leave voice, and create a channel. Record click count, dead ends, focus loss, and clipping.
2. **Conversation pass:** Responsive panes, composer send affordance, search destination, and actionable empty/error states. Validate each task with pointer and keyboard.
3. **Access pass:** Message keyboard actions, contrast, Settings controls for scale/motion, and persistent operation feedback. Verify with accessibility tooling and manual focus traversal.
4. **Administration pass:** Channel creation and settings save/discard flows, then artwork loading stability.
5. **Polish pass:** Tune typography, spacing, and motion against real captures. Update the repository screenshot only after the interface represents the current build.

## Release gate

Run QML lint, client build, and relevant existing tests for changed behavior. Then test the five baseline tasks on a live local sandbox at minimum and normal window sizes, with keyboard only and pointer only. Include offline/reconnect, read-only channel, long names, pending uploads, screen sharing, and reduced motion. For changes that affect media or voice, exercise the installed GUI rather than relying on an offscreen screenshot alone.

Success means a first-time user can complete the five tasks without hidden knowledge, an experienced user keeps the fast keyboard path, and no critical control disappears at the minimum supported window size.


### Artwork cache lookup responsiveness — 2026-10-11

Cache-hit validation now decodes on workers, capped at two outstanding jobs with
per-ID deduplicated backpressure. GUI-thread completion checks request ownership,
authorization, account generation and nanosecond file identity before publishing
a URL or removing corrupt data. Worker reads use no-follow directory-relative
file descriptors and never mutate the cache. Cached offline restart remains
supported. Download/store validation and directory pruning are still synchronous;
this is the lookup slice, not a claim that all cache I/O is asynchronous.
Validation and installation evidence: `docs/desktop-session-handoff.md` and
`build/desktop-async-cache/`.

### Downloaded artwork responsiveness — 2026-10-11

Downloaded staging and duplicate destination images now decode off-thread in the
same two process-wide worker slots as cache hits. Staging ownership and per-ID
serials persist through backpressure, while GUI completion rechecks generation,
authorization and both file identities before mutation/publication. Real-daemon
blocked-worker checks cover responsiveness, deduplication, normal publication,
revocation and controller destruction; offline reuse remains covered. Directory
pruning/reconciliation and final file operations remain synchronous.
Release 314/314, clean package 299+15, native/package-native 4/4 and both lint
checks pass. Fresh main/debug packages are installed and verified; the upgrade
hook disconnected the previously joined voice session. Evidence:
`build/desktop-async-store/` and `docs/desktop-session-handoff.md`.

### Artwork reconciliation responsiveness — 2026-10-11

Authorization updates are state-only and immediately clear revoked previews.
Read-only directory scans/eviction planning use bounded shared workers; the owner
applies revision/file/directory-guarded deletion in batches of at most 32 candidates
per event-loop turn. Stale account/access/staging/store/LRU snapshots and destroyed
controllers cannot apply cleanup. Store-time pruning/final file operations remain
synchronous; batch size does not guarantee latency on slow storage.
Release 320/320, cache 28/28, native/package-native 6/6, package 305+15 and both
lint targets pass. Fresh main/debug packages are installed and file-verified,
connected and visible. See docs/desktop-session-handoff.md and
build/desktop-async-reconcile/. No server changes or publication.

### Store-time artwork pruning responsiveness — 2026-10-11

Store-time enumeration and sorting now run on bounded workers. Incoming byte/entry
reservations, current pins and stale-plan rescans gate publication; owner-thread
cleanup applies at most 32 guarded candidates per turn and retains request/staging
ownership between turns. Incomplete enumeration rejects admission. Final file
operations remain synchronous; candidate count cannot bound slow-storage latency.
Release coverage 318+10, cache 35/35, native/package-native 7/7, package 313+15 and
both lint targets pass. Fresh main/debug packages are installed and file-verified,
connected and visible. Initial broad audio-frequency failure and successful
isolated/final reruns are retained in the handoff; voice code is unchanged.
Self-review/evidence: `build/desktop-store-pruning/`. Human usability acceptance
remains separate. No server source or publication changes occur in this slice.
