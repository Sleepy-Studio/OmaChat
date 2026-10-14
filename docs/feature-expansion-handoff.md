# OmaChat feature expansion handoff

Updated: 2026-10-14. Full user-capability audit + expansion backlog.
Resume from this file. Source of truth for behavior is the working tree;
roadmap docs remain the scope record.


## CI permission regression repaired — 2026-10-14

The four failed assertions reproduce exactly when the artwork enumeration test
runs as root. CI's Arch container runs its unit/integration steps as root;
CAP_DAC_OVERRIDE allows enumeration of the chmod-000 directory. The same test
passes as the ordinary desktop user. No production cache change is required.

`.github/workflows/ci.yml` now creates the existing GUI test user immediately
after building and runs unit/fuzz, integration and GUI checks under that account.
The permission regression remains unchanged and is exercised rather than skipped.
Current test targets rebuild successfully; all **202 unit/fuzz checks** pass
(the opt-in real-keyring round trip is skipped), and all 36 ArtworkCache-matched
checks pass through a root-to-user `su` invocation. Actionlint and whitespace pass.
Evidence: `build/desktop-ci-permissions/evidence/`.

This CI/documentation-only repair is being pushed to the desktop branch for
exact-SHA verification. No application change, local reinstall or main-branch
push is needed. Fresh local status is connected with `artwork.framing.v1` and
**voice/screen sharing active**; no restart or deployment is performed.

**Next:** verify the repaired remote CI, then implement/test stale-keyring-session
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

### P0 — close Phase 1 leftovers (verify first, then build)
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

1. P0 verify (`928983a` + dirty) -> record. No new protocol until green.
2. P0 focal/variants + whole-list revision + conflict UX (one owner per file; coordinator owns integration/CMake/docs/packages/deploy).
3. P1 notifications/mute-duration + favorites/hide/unread (highest user value, mostly client/daemon-local).
4. P2 pins + slow mode + archive (server-backed; each needs migration + capability + CLI + tests).
5. P5 visual follow-ons interleaved as small wins between heavy slices.
6. P4 admin (preview -> sync -> templates -> audit -> bulk -> import/export).
7. P3 voice controls after two-machine voice/screen baseline measured.
8. P6 MLS/SFrame last, with independent review; no UI label changes until gates pass.

## Next session start here

1. `git status`, `git log --oneline -5`; confirm whether `928983a` + dirty tree is the intended base or needs stashing/worktree split.
2. Run focused tests for touched areas first (`test_permission_preview`, `test_placement_revision`, `test_permission_sources/targets`), then one combined checkpoint (not per-worker full suites). Bound compiler load (`-j6` recent), serialize native capture/emulator/deploy/install.
3. For server changes: commit/push to `main`, wait exact-SHA Docker publication before forcing Coolify deploy, then verify health + live capability. Client-only: local package install + file/binary match checkpoint.
4. Refresh this file + `docs/desktop-session-handoff.md` + vault (`wiki/hot.md`, index, session page) with exact test/rollout evidence, installed-vs-source-only state, remaining blockers. Preserve dirty tree + ignored `session-ses_f0f5.md`.
