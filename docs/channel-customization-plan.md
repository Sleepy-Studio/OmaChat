# Channel customization plan


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

Status: staged roadmap, reviewed 2026-10-11. The channel identity increment, bounded artwork cache and conservative safe category deletion are implemented in this working tree; the tables below include work still open.

The October 7 client-only continuation is reviewed and installed locally from a
recorded uncommitted source snapshot based on `d35634a`. It adds creation crop
staging, bounded placement edge scrolling, quick-switcher icons and existing
joined-voice identity icons. The final package build passes 291/291 CTest, both
QML lint targets and 13 native Wayland checks; all 126 installed artifacts match,
with zero package alterations. The hosted October 6 server stays on `891e4c2`.
See [desktop-session-handoff.md](desktop-session-handoff.md) for exact provenance,
installed/source state and remaining gates. This does not complete later phases.

The next October 7 client slice installs current-user inherited-access review
before creation: server/category masks, type-specific actions, live/offline
refresh and keyboard viewport focus. Package 293/293, both QML lint targets and
6/6 native Wayland permission checks pass; all 126 installed artifacts match.
Full source attribution and other-member creation previews remain open.
Evidence: `build/desktop-access-review/` and the desktop handoff.

The latest October 7 installed slice adds Overview/Appearance/Permissions channel
settings navigation and a joined-voice details action with a stable room banner,
loading/retry/fallback and bounded description surface. Draft/CAS/discard state
survives section changes and the nested overrides editor. Context/access loss
clears details; cached offline content is read-only. Release 304/304, both QML
lint targets, native 11/11 and exact verification of 126 installed artifacts pass.
See `build/desktop-room-settings/` and the handoff. The existing server was
restarted to repair connectivity; no new server implementation was published.

The October 11 continuation installs bounded asynchronous validation for both
cached and downloaded artwork. Shared worker slots, retained staging ownership,
request serials and file-identity checks protect delayed completion. Directory
pruning and final filesystem operations remain synchronous. Release 314/314,
package 299+15 and native/package-native 4/4 pass; all installed artifacts match.
See `build/desktop-async-store/` and the latest desktop handoff.

The latest October 11 slice installs store-time pruning on the shared bounded
worker path, with incoming capacity reservations, stale-plan rescan and guarded
32-candidate owner-thread cleanup batches. All directory scans and artwork
validation are off-thread; final file operations still run on the owner thread.
Incomplete enumeration or insufficient pinned capacity rejects admission.
Final Release coverage 318+10, cache 35/35, package 313+15 and native/package-native
7/7 pass. Fresh packages are locally installed and all 126 artifacts match.
Self-review and evidence: `build/desktop-store-pruning/` and the desktop handoff.

### Implemented in the first increment

- Channel icons and banners for text, voice, and category channels, with authenticated uploads and permission checked downloads.
- Separate descriptions for channels and categories, shown in details and editable with name and topic in channel settings.
- Initial category, topic, description, icon, and banner choices during channel creation.
- Moving channels between categories and reordering siblings through menu actions; server stores positions transactionally.
- Server validation and image normalization, schema migration, protocol fields, daemon methods, GUI controls, and CLI commands.
- Account/endpoint/user/certificate-pin scoped artwork cache: 64 MiB/256 completed entries globally, active-file protection, corruption/replacement/access-loss handling and offline GUI restart.
- Keyboard Menu/Shift+F10 moves and category activation; transactional category deletion moves children to top level, preserving messages/child overrides. Nonempty categories with inherited overrides require explicit review/moves before deletion.

- Exact keyboard/pointer placement dialog with atomic server-resolved before-sibling/end ordering, including hidden siblings; capability-gated CLI parity.
- Current-user effective-permission review from authoritative server masks, category path explanation, read-only member access and keyboard scrolling. Other-member/source previews remain open.
- Bounded pre-upload channel-settings crop with pointer/keyboard position, zoom and canonical icon/banner sizes; source/account fingerprint guards and pending/error feedback. This is a flattened asset, not persisted focal-point metadata or multiple server variants.

### Desktop continuation — 2026-10-06

- Pointer placement now uses a thresholded sidebar gesture, visible destination
  cue and server-resolved before/end operation. It supports channel moves into
  and out of categories and category ordering. Pending/rejected drops retain
  authoritative order; Escape, invalid targets and context changes cancel.
  The October 7 client continuation adds bounded in-list edge scrolling with stationary-pointer target resolution.
- Server settings icon/banner pickers use the shared bounded asynchronous crop
  review and canonical image sizes. Preview fingerprints also bind the selected
  server; stale jobs, changed sources and lost server authority cannot submit.
- Channel name/topic/description saves use capability-gated atomic comparison of
  all three original values. Conflicts retain the draft. Explicit Reload latest
  retains local edits and shows authoritative values inside the scrolling form;
  only a deliberate subsequent Save can overwrite those values. Old protocol
  clients keep their existing behavior. Checked desktop saves require an updated
  server. These comparisons cover detail fields, not artwork or placement.

### Still open in Phase 1

- Persisted focal-point metadata and image variants. A voice-room details banner surface is now installed. October 7 adds initial-creation crop staging and quick-switcher/joined-voice identity icons; validation/install provenance is in the desktop handoff. Cache/download validation and reconciliation/store-pruning scans now run off the GUI thread with two bounded worker slots and stale-result guards. Final filesystem operations remain synchronous.
- Named contributing-role attribution, role-only previews and full creation access previews. Existing-member source-stage previews and Overview/Appearance/Permissions settings navigation are installed. Exact keyboard placement and current-user review are implemented. Category move-to-top-level/Cancel is implemented; preserving inherited overrides automatically during deletion remains open.
- Whole-list placement revision comparison and hidden-sibling conflict explanations beyond source parent/position CAS. Atomic duplication and checked artwork/placement saves are implemented in the October 12 continuation.
- Full permission-source and other-member review before channel creation. Current-user inherited-access preview is installed in the latest October 7 slice.

The later phases remain proposals. They have not been implemented by this increment.

## Baseline before this increment

omachat has text and voice channels, one level of categories, stored positions,
topics, role/member permission overrides, local category collapse and channel
mute, and server-side create/update/delete. The GUI can create channels and
categories, set a topic, edit overrides, mute, and delete. It has no channel
icons, banners, full descriptions, server-channel settings view,
rename/move/reorder controls, or graduated notification choices. Existing
channel wire fields are `name`, `type`, `parent_id`, `position`, and `topic`;
the update request changes only name and topic. The attachment pipeline can
upload images, but channel artwork cannot simply reuse message attachment
visibility rules.

## Product rules

- Scope personal preferences to an account; shared channel settings live on
  the server and are enforced there.
- Show a setting only where it applies. Text composition rules do not appear
  on voice channels; voice capacity does not appear on categories.
- Use the existing role and member overrides. A settings editor should show
  effective permissions and their source before adding new permission bits.
- Keep defaults equivalent to today's behavior. Old clients should still
  connect and see a usable channel; gate new requests with capabilities.
- Keep channel names and hierarchy readable at narrow sidebar widths and via
  keyboard and screen reader. Artwork is optional and never the only way to
  identify a channel. Contrast, reduced motion, and image-loading limits apply.
- Server channels remain searchable and server readable under the existing
  security model; customization must not imply end-to-end encryption.

## Phase 1 — channel identity and management foundation

Ship these together as the first useful increment.

| Feature | Behavior and scope | Acceptance check |
|---|---|---|
| Channel icon | Optional uploaded image for text and voice channels. Show a small crop in the sidebar, header, quick switcher, and channel details; keep the existing hash/speaker fallback. | Missing, denied, or corrupt images fall back cleanly; narrow rows remain readable. |
| Channel banner | Optional wide cover image in channel details and an expandable header, with a focal-point/crop selector. Voice channels show it in their room details. | A banner never obscures the channel name or composer and does not stretch at different window sizes. |
| Full description | Add a separate multiline description for purpose, rules, and useful links. Keep the existing topic as the short header summary. Render the existing safe Markdown subset. | Description is visible on first visit and from the header, and the server validates its length. |
| Category image and description | Optional category icon/cover and short description in category details; preserve the compact sidebar row by default. | Collapsing and navigating a category stays quick with or without artwork. |
| Unified channel settings | Context menu opens Overview, Appearance, and Permissions; a Notifications section hosts today's mute control and expands in Phase 2. Overview edits name, topic, and description with character counts and save/cancel state. Appearance previews icon/banner crops. | A manager can edit text, voice, and category identity; a member cannot submit edits. |
| Move and reorder | Keyboard move actions and pointer drag/drop place a channel in a category or top level and order siblings; categories reorder among categories. Persist server-wide. | Two clients see the same order after reconnect; permission failure leaves original order intact. |
| Category management | Rename a category, move a channel in/out, and offer a choice when deleting a nonempty category: move children to top level or cancel. Existing collapse stays per user. | No child becomes invisible or loses its overrides during a move. |
| Channel creation options | Creation dialog offers initial category, topic, description, optional artwork, and a review step for inherited access; keep minimal defaults. | New channel lands in the chosen place and its access preview agrees with server enforcement. |
| Duplicate channel configuration | Copy type, topic, description, artwork references, chosen category, and permission overrides into a new channel; never copy messages or members. Preview the new name and access. | Copying a restricted channel does not briefly publish it to unauthorized users or duplicate image files needlessly. |
| Undo-safe edits | Use request responses to show field errors; refresh from authoritative events, and warn before discarding unsaved edits. | Concurrent edits are visible and do not silently overwrite another admin's changes. |

### Artwork and description contract

- Use server-hosted uploads through the existing transfer path, with a new
  channel-artwork claim/reference and download authorization based on
  `ViewChannel`. Do not require members to fetch an administrator's external
  image URL. Never expose private channel artwork through a public URL.
- Accept static PNG, JPEG, and WebP after decoding and checking actual image
  content, dimensions, and byte limits. Make a small icon variant and a bounded
  banner variant; reject oversized or decompression-bomb images. Strip image
  metadata and keep original/cropped assets only as needed. Animated images
  can be a later opt-in feature.
- Store icon and banner attachment IDs, optional crop/focal coordinates, and a
  separate description in additive schema and protobuf fields. Define clear
  semantics for removing an image, replacing it, and reclaiming unreferenced
  files. Copies may reference a shared immutable asset with reference counts.
- Images load lazily and are cached by asset ID/version, with a bounded disk
  cache. A channel event invalidates the correct cache entry. Provide a
  placeholder and alt text (channel name) while loading or when offline.
- A member who loses `ViewChannel` must lose image download access on the
  server. Test permission changes, category moves, resume/reconnect, copied
  channels, and removal from a server. Server audit records should identify
  the editor without storing image bytes in the log.
- Bound descriptions separately from the 512-character topic (suggested
  maximum: 2,000 characters). Render sanitized Markdown; do not embed remote
  images or active content in descriptions.

### Visual follow-ons after the core artwork release

| Feature | Behavior and scope | Acceptance check |
|---|---|---|
| Channel accent | Optional server-shared accent used sparingly for the header and details. Derive readable text colors and offer a local "use theme colors" override. | Text and controls meet contrast targets in light/dark themes and high-contrast mode. |
| Emoji icon | Choose a Unicode or server emoji as a light alternative to an uploaded icon. Fall back to the channel type icon if the emoji is removed. | Sidebar and quick switcher render the same fallback after an emoji deletion. |
| Welcome card | Optional first-visit card that combines banner, description, key links, and pinned rules; members can reopen or dismiss it. | Returning members are not forced through it on every visit. |
| Channel links | A small curated list of labeled HTTPS links in details, each opened through the existing external-link flow. | Invalid schemes and misleading labels are rejected or clearly shown. |
| Personal chat background | Optional account/device-local solid tint or static image behind messages with opacity control; never uploaded or imposed on other members. | Message contrast and scrolling stay sound; high-contrast mode can suppress the background. |
| Banner visibility | Personal compact/expanded/hidden banner choice, independent of the server's chosen image. | Compact mode keeps the composer and latest messages visible in a short window. |

Implementation: add explicit move/reorder fields or a dedicated atomic request,
with validated parent, sibling position, and an update revision. Store dense
positions transactionally for affected siblings. Resolve category visibility
for the moving actor and every audience member, then publish correct
create/update/delete visibility events (or trigger a scoped resync). Add CLI
parity for rename, move, and reorder. A normal channel deletion remains a
separate destructive action.

## Phase 2 — member-level organization and notifications

These settings should follow the account across devices where practical.
The first delivery can keep them in the local daemon database if the UI says
they are device-local; syncing requires a small account-preferences protocol.

| Feature | Behavior and scope | Acceptance check |
|---|---|---|
| Notification levels | All messages, mentions only, and none, with a separate option for `@everyone`/role mentions if those mentions are later implemented. Replace the current mute boolean. | A muted channel does not create unread emphasis or desktop alerts; mention counts follow the selected rule. |
| Mute duration | 1 hour, 8 hours, 24 hours, until changed. Display expiry and automatically restore the prior level. | Restart/reconnect preserves an active timer; expiration changes behavior once. |
| Favorites | Pin selected channels to a personal Favorites section; retain canonical server order below. | Favorites do not change other members' sidebars or channel permissions. |
| Hide and collapse | Hide a channel from the personal sidebar without leaving it; show hidden channels in a manage view/search. Per-user category collapse persists. | Hidden unread channels remain discoverable and notification rules still apply. |
| Unread controls | Mark channel/category read, jump to first unread, and optional unread-only sidebar filter. | Read markers survive reconnect and do not alter another member's state. |
| Activity density | Per-user choice to show/hide voice participants under channels and compact the sidebar. | Voice state remains reachable through channel details and accessibility labels. |

## Phase 3 — text channel behavior

| Feature | Behavior and scope | Acceptance check |
|---|---|---|
| Slow mode | Server-enforced interval between a member's new posts, configurable from off to several minutes. Edits and moderator actions do not reset it; show remaining time in composer. | Two clients on one account cannot bypass it; reconnect does not reset the timer. |
| Posting policy | Channel-level `Send Messages`, `Attach Files`, links/embeds, reactions, and thread creation controls. Reuse permission overrides for access; add policy fields only for behavior that cannot be expressed as a permission. | GUI disables prohibited actions and server rejects direct protocol attempts. |
| Pinned messages | Managers pin/unpin with a bounded per-channel limit; all members who can read history can open the pin list. | Deleted/inaccessible messages disappear from pins; pin changes sync live. |
| Channel guide | Extend the Phase 1 description with an optional structured guidelines panel or welcome checklist, shown on first visit and from the header. Topic stays a concise one-line summary. | A member can reopen it; edits appear across clients. |
| Channel-specific retention | Optional server-owner policy: keep indefinitely or delete after a fixed period, with a preview of affected history and clear exceptions for legal/operational needs. | Background deletion handles attachments, search index, replies, and audit records consistently. |
| Archived/read-only state | Managers can stop new posts without deleting history, with a visible reason and optional reopening. | Search and history remain available to permitted members; sends fail server-side. |
| Announcement channel | A distinct posting permission, follow/notify option, and visible announcement styling; no automatic crossposting in its first version. | Only permitted publishers post; ordinary members can read/react if allowed. |
| Threads | Replies can branch into a named, subscribable thread with its own unread state and archive rule. | Existing flat replies keep working; thread history and permissions follow the parent. |
| Forum channel | Later type built on threads: post list, optional tags, sort/filter, archive and reopen. | Large forums page results; tag permissions and search are enforced server-side. |

Slow mode, pins, guide, retention, archive, and new channel types need schema
migrations, protobuf fields, daemon JSON mapping, CLI commands, UI, and
integration tests. Do not overload the `topic` field with long descriptions.

## Phase 4 — voice channel behavior

| Feature | Behavior and scope | Acceptance check |
|---|---|---|
| User limit | Optional capacity; managers with the appropriate permission may bypass only if explicitly defined. | Simultaneous joins cannot exceed the limit; full state is visible before joining. |
| Voice quality preset | Per-channel target bitrate within server limits; the relay announces the actual chosen rate. Avoid exposing arbitrary codec knobs. | Two-machine voice test confirms the negotiated value and stable playback. |
| Stream quality cap | Channel cap on resolution/frame rate and concurrent streams, subordinate to server policy. | Publisher sees effective cap; direct protocol requests cannot exceed it. |
| Join/speak defaults | Easy presets for open conversation, listen-only, and moderator-led rooms, implemented through existing permissions. | A member without Speak cannot publish audio even with a modified client. |
| Channel status | Optional temporary status text such as meeting title; distinguish it from the persistent topic. | Expires or clears predictably and syncs live. |
| Temporary voice rooms | Authorized users create rooms from a template; idle rooms expire after a grace period, never while occupied. | Restart does not strand or prematurely delete an occupied room. |
| Voice lobby | Optional waiting area with explicit admission by authorized moderators. | A waiting member cannot receive room audio/video before admission. |

Voice limits and quality controls require coordinated server, daemon, and
media changes. Ship them after the two-machine voice/screen-sharing baseline
in `status.md` is measured; avoid promising quality benefits from a setting
alone.

## Phase 5 — power administration

| Feature | Behavior and scope | Acceptance check |
|---|---|---|
| Permission preview | "View as role/member" explains effective View, History, Send, Connect, Speak, Stream, and Manage permissions, including category and channel sources. | Preview matches actual server decisions in a role/override test matrix. |
| Category permission sync | Admin may copy category overrides to children or choose inheritance; show which children are custom. Never silently replace child overrides. | Explicit preview lists every changed channel and can be canceled. |
| Templates | Save a reusable channel setup with type, settings, and role-based overrides. Member-specific overrides require deliberate opt-in. | Template application is atomic and does not leak a restricted channel. |
| Audit log | Record who changed channel settings, permissions, moves, pins, archive, and retention, with old/new values and time. Owner controls access and retention. | Every accepted admin mutation has one entry; rejected attempts do not masquerade as changes. |
| Bulk edit | Multi-select channels to move, archive, or apply a template, with a diff preview. Personal notification choices stay outside this admin action. | Partial failure is reported per channel or transaction rolls back as documented. |
| Import/export configuration | Export channel tree and settings without messages or secrets; import with ID mapping and collision preview. | Reimport does not duplicate channels unless explicitly requested. |

## Delivery order and release gates

1. **Channel identity and foundation:** icon, banner, full description,
   settings view, rename, move/reorder, safe category editing, and duplicate
   configuration. Ship the image authorization and cache work with the UI.
2. **Personal controls and visual follow-ons:** notification levels/durations,
   favorites, hidden channels, read controls, restrained accent/emoji choices,
   welcome cards, and optional personal background. Decide local versus synced
   storage before UI copy.
3. **Text controls:** slow mode, pins, guide, archive. Treat retention,
   announcements, threads, and forums as separate releases.
4. **Voice controls:** capacity and permission presets first; quality and
   temporary/lobby behavior after live media validation.
5. **Administration:** permission preview before templates and bulk changes;
   audit log before retention or large-scale edits.

For each server-backed release: migrate a copied schema-v7 database, verify
old-client behavior and capability negotiation, exercise unauthorized direct
requests, test reconnect/resume and two clients receiving updates, and run the
full build/tests plus a GUI keyboard/screen-reader pass. Test moves and
permission changes with hidden channels and category overrides. Test image
upload/replace/delete with invalid formats, very large dimensions, offline
loading, lost access, and cache invalidation. Keep schema changes additive
and provide a backup/rollback plan for self-hosted servers.

## Decisions to settle before implementation

- Whether personal organization and notification settings must sync between
  devices in the first release.
- Whether moving a channel changes category permission inheritance
  immediately (recommended: yes, with a preview of members whose access
  changes).
- Whether retention and audit are available to all self-hosted servers or
  remain owner-only advanced settings.
- Whether threads and forums belong in the same roadmap milestone as basic
  channel customization (recommended: separate releases).
- Exact icon/banner dimensions and storage limits after testing representative
  images on narrow and large windows; the limits should be server configurable
  within safe bounds.

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
