# Phase 2: personal organization and notifications

Status: 2A and 2B installed and validated, 2026-10-15. Device-local notification
and sidebar preferences use transactional local schema 4, checked daemon IPC,
CLI and native GUI. All 469 package CTest checks and 3 package-built native Wayland
checks pass. All 129 installed artifacts and both running binaries match; schema 4
and both account reconnections are verified. Installed sidebar IPC/CLI round trips
pass with preferences restored exactly. Next is 2C unread recovery and controls.
See desktop-session-handoff.md for evidence and remaining gates.
The source audit below records the pre-implementation baseline. Baseline: `87822ff` plus the installed, uncommitted
keyring recovery fix. Phase 1 installed/live artwork acceptance is complete;
independent human usability acceptance remains open.

## Scope and storage choice

Use the daemon's local SQLite store for notification and sidebar preferences,
keyed by local account ID and channel ID. Label the settings **On this device**.
This is the implementation default selected for this plan from the handoff's
local-first recommendation, not a claim that account preference sync exists.
Account removal cascades these preferences; logout/reconnect and GUI closure do
not clear them. A second account on the same host must never inherit them.

Read markers retain their existing server-backed semantics. Device-local sidebar
preferences must not introduce a competing authority for server read state.
Role/everyone mention controls require a separate protocol slice. Favorites,
hiding, and collapse do not change server placement, visibility permissions,
membership, or notification eligibility.

## Source audit

| Existing path | Current behavior | Implementation implication |
| --- | --- | --- |
| `daemon/src/storage/LocalStore.cpp` | Local schema 2; `muted_channels` stores account/channel pairs | Introduce a checked, transactional migration; preserve indefinite mutes |
| `daemon/src/application/DaemonMethods.cpp`, `channel.mute` | Writes a boolean mute; ignores store failure before broadcasting success | New writes acknowledge and broadcast only after persistence succeeds |
| `daemon/src/application/Daemon.cpp`, `maybeNotify` | Suppresses self, DND, muted and focused-channel messages; otherwise DMs/mentions gated by global settings | Keep these suppressors and current defaults; centralize the new policy |
| `Daemon.cpp`, `onBackgroundEvent`, `activate`, synchronization callback | Background links cache mutes; account activation resets aggregate activity | Evaluate the same policy for every account and refresh all cached state |
| `client/src/controllers/AppController.cpp` | Snapshot reads legacy mutes; unread buffers/counters are in memory; read-marker events trim those buffers | Snapshot/event contracts need account identity; read-marker persistence alone does not reconstruct unread counts |
| `client/src/controllers/AppActions.cpp`, `toggleCategory` | Collapse is an in-memory set keyed by channel ID | Persist with account scope and reload on account switch |
| `client/qml/views/ChannelSidebar.qml` | Boolean mute action, collapsed categories and voice participants | Extend existing channel menu and model roles; keep keyboard access |
| `cli/src/main.cpp` | `channel mute/unmute` calls legacy boolean method | Preserve these commands and add explicit preferences/duration options |
| `daemon/src/networking/ClientState.cpp`, `messageJson` | User mention IDs / `mentions_me`; encrypted mentions computed locally | Do not infer trusted role/everyone mention kinds from raw text |
| `server/src/core/MessageHandlers.cpp`, `handleSetReadMarker` | Checks message/channel access and persists monotonic read progress | Reuse `read.markers.v1`; never move a marker backwards |

These findings are source inspection, not new runtime or test results.

## Slice 2A: notification levels and timed mute

Deliver this slice before sidebar organization. No server schema, network
protocol or Android change is required.

Persist a `channel_preferences` row per `(account_id, channel_id)` with:

- `notification_level`: `default`, `all`, `mentions`, or `none`. `default`
  preserves today's behavior: community mentions and DM/group messages follow
  their respective global toggles. Explicit levels are intentional channel
  overrides, while global notification toggles remain master opt-outs.
- `mute_until_ms`: nullable UTC epoch milliseconds; null means unmuted, zero
  means muted until changed, positive means muted before that instant. Allowed
  duration inputs are 1h, 8h, 24h and until changed. Compute the deadline in the
  daemon from its clock, rather than trusting a GUI-generated timestamp.

`none` is a durable notification level. Temporary mute overlays the selected
level and expiry restores it. Migration maps every legacy mute to an indefinite
overlay with `default` underneath. Unmute clears only that overlay. The legacy
`channel.mute` method remains an adapter; new code reads the new table as the
single authority. Document that older daemons cannot open the new schema.

Notification policy, evaluated in this order:

1. Reject self-authored messages, unknown/inaccessible channels, global daemon
   notification disable, DND, and the active focused conversation.
2. Apply the mute overlay using the current UTC time; equality with the deadline
   is expired. Apply `none` next. Neither hiding nor favorites affect delivery.
3. For `default`, retain existing DM/group and mention behavior. For `mentions`,
   require an individual mention (including locally decoded encrypted mentions).
   For `all`, allow ordinary community messages as well. The existing global
   DM toggle gates DM/group delivery; the existing mention toggle gates community
   delivery, including `all`. Update its UI label/help to explain that scope.
4. Do not replay suppressed messages when a mute expires or the device wakes.

Use an injected time in policy tests. A daemon timer refreshes effective mute
state at the next expiry with a bounded periodic recheck for clock changes and
resume. Every notification decision also evaluates expiry directly, so a delayed
timer cannot suppress newly eligible messages. Startup, reconnect, account
activation and GUI snapshots recompute effective state.

Retain the old `muted_channels` snapshot field and `channel.muted` event as
effective-overlay projections for old clients. Add `channel.preferences.get/set`
IPC methods, a preferences snapshot, and an account-tagged
`channel.preferences_changed` event carrying stored and effective values. Add
account identity to legacy events too. New clients reject events for a different
active account; background notification policy uses the originating account.
Keep IDs as JSON strings. Reject invalid enums, durations and unknown fields;
return a storage error without mutating caches or broadcasting on write failure.

GUI: extend the existing channel menu with notification level and mute duration,
show the local expiry and **On this device**, and expose Unmute. CLI: preserve
`channel mute/unmute`, add `channel mute CHANNEL --for 1h|8h|24h|forever` and
`channel notifications CHANNEL default|all|mentions|none` with JSON results.
Muted unread presentation remains compatible in this slice; separate unread
state from notification policy in 2C without silently changing existing badges.

Acceptance gates:

- Migrate a real schema-2 fixture, reopen it, preserve accounts/trust/known keys,
  volumes and mutes; verify account cascade and isolation. A failed migration
  rolls back entirely and does not advance `user_version`.
- Matrix-test DM/group/community, all levels, global toggles, self, DND, focus,
  individual/encrypted mentions and active/background accounts.
- Test expiry before/at/after deadline, restart while muted, restart after expiry,
  time moving forwards/backwards, resume and reconnect without notification replay.
- Test failed persistence, legacy IPC projections, simultaneous GUI clients and
  account switches with colliding channel IDs. No event may alter another account.
- Native dark/light keyboard and pointer checks at 720×460/150% and
  1440×900/100%; show expiry and restore the prior level after a short test mute.
  Use an injected clock or isolated test harness, not a production clock change.

Primary implementation files: LocalStore, Daemon/DaemonMethods, AppController/
AppActions, ChannelSidebar, CLI, `tests/unit/test_store.cpp`, new notification
policy unit tests, and daemon IPC integration coverage. Add build registrations
in the existing CMake test targets. Test the migration and policy before wiring UI.

## Slice 2B: favorites, hiding and persistent sidebar state

Add account-scoped preference fields for favorite rank, hidden and category
collapse, plus view settings for voice participants and compact sidebar. Favorites
use a separate section with deterministic ordering and keyboard move actions;
do not change the server's channel positions or duplicate a channel in navigation.

Hiding removes a channel from the ordinary sidebar only. Add an accessible
**Hidden channels** management view and searchable discovery with Unhide. A hidden
channel can still notify and open from search or notification; keep its selected
row visible while open. Never hide the sole route to the active voice call's
leave/mute controls. Losing View permission removes any display of the channel
and its artwork even if a saved preference exists; restoring permission can
restore the personal preference. Clear preferences on confirmed deletion, not a
temporary omission from a partial snapshot.

Test persistence/reconnect/account isolation, favorites reorder, keyboard focus,
hidden-channel discovery, search/notification navigation, permission revocation,
active voice access and category collapse with a selected child. Keep hiding
entirely separate from notification mute.

### 2B implementation contract (2026-10-15)

Local schema 4 adds `sidebar_channels` (favorite rank, hidden, collapsed) and
`sidebar_views` (compact rows, show voice participants), both with account cascade.
The schema-3 migration is transactional; notification preferences, accounts,
trust, keys and volumes stay in their existing tables. Older daemons cannot open
schema 4. Sidebar writes replace the account's sidebar state in one transaction;
failed writes emit no success and leave the prior state intact.

`sidebar.get`, checked `sidebar.set`, `sidebar_preferences` snapshots and
account-tagged `sidebar.changed` events expose the same authority to every GUI
and CLI. Favorites are ordered by rank then channel ID, appear once in a section
within the current server/home, and keep their channel identity as keyboard focus
moves. GUI move requests carry a validated `selected_channel` hint so hidden
selected channels and hidden active voice channels remain reorder candidates.
Server placement gestures exclude favorite rows; explicit administration actions
still operate on server placement.

The sidebar settings button opens **Sidebar — On this device**, including a
searchable Hidden channels list with Open/Unhide and compact/participants toggles.
Quick switcher discovery includes hidden channels. Selected channels and the
active call survive hiding/collapse. Permission resync removes channel/artwork
presentation while preserving preferences; confirmed channel deletion clears
preferences for active and background accounts. Temporary omission is not deletion.

CLI: `channel favorite CHANNEL [up|down|remove]`, `channel hide|unhide CHANNEL`,
`sidebar get`, `sidebar compact on|off`, and `sidebar participants on|off` support
`--json`. Hiding and favorites do not alter notification policy or server markers.

## Slice 2C: unread controls and recovery

First establish authoritative per-account unread state across GUI closure,
daemon restart and reconnect. Reuse server markers and paged history; reconcile
against marker `(timestamp, message_id)` ordering, ignore self messages and
deduplicate history/live overlap. Do not announce exact counts or a first-unread
target from an incomplete history window. Determine during implementation whether
bounded paging suffices; if a server unread-summary endpoint is needed, make it
a separate capability-backed server slice with rollout verification.

Then add mark-channel-read, mark-category-read, first-unread navigation and the
unread-only filter. Capture each channel's target at invocation so messages
arriving during the operation remain unread. Category operations report per-channel
failures; offline work remains pending until acknowledged. Do not optimistically
erase unread state on server rejection. A newer marker from another device can
only advance progress. An older server without `read.markers.v1` must receive an
explicit limited-persistence indication, not a false successful durable mark.

Test equal timestamps, deleted marker messages, empty/read-only/inaccessible
channels, more than one history page, duplicate deliveries, group DMs, concurrent
messages/markers, account switching, failure/retry and old-server behavior.

## Slice 2D: mention kinds

Define role/everyone semantics and permission checks before exposing switches.
The current user-ID mention list does not preserve those kinds. Introduce a
versioned additive protocol and authoritative server classification, with client
fallback and dedicated encrypted-conversation semantics. Require a server rollout
and compatibility tests; do not bundle it into the local-only 2A package.

## Execution and completion evidence

Next: implement 2C authoritative unread recovery before unread controls. Keep 2D
mention semantics separate. Preserve existing source and ignored evidence.
Run affected tests, format/QML checks and a clean package verification before
installing. Recheck live voice/sharing immediately before any restart; serialize
native GUI and installation work. Record installed artifact/runtime parity and
native acceptance separately from automated results. A client-only slice needs
no hosted deployment. Any later server slice follows the standing commit/push and
verified rollout workflow. Public releases/AUR and vault Git commits stay outside
this continuation. Human usability, physical Android and MLS/SFrame gates remain
open and independent.
