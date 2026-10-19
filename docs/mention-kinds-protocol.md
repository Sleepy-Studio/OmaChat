# Phase 2D: mention kinds and attention policy

Status: implemented in the continuation working tree, 2026-10-18; validation
and deployment evidence are tracked in the session handoff. Source presence does
not establish production rollout. Server schema 16 adds mention snapshots (schema
17 also includes incoming webhooks); local schema 5 adds the two checked device
preferences. The wire allocation is `ChatMessage.mention_classification = 13`,
`Envelope.get_attention_summary = 165`, and `Envelope.attention_summary = 166`.

`MentionClassification` version 1 uses kind/reason bits 1 (individual), 2 (role),
and 4 (everyone). `AttentionSummary.unread` retains the original complete unread
snapshot, `mask_counts` has exactly seven uint64 values indexed by mask minus
one, and `classification_complete` is independent of individual completeness.
The daemon exposes mask totals as decimal strings. Event replay stores the
original event body and its private recipient masks together, so subsequent
edits and role changes cannot rewrite a replayed create revision.

## Preimplementation evidence and compatibility constraints

Source audit recorded before this implementation:

- `server/src/core/MessageHandlers.cpp::extractMentions` resolves plaintext
  `@username` tokens to accessible users, deduplicates IDs and limits to 50.
  Send and edit classify on the server. There is no role/everyone authority.
  The legacy regex can also truncate an overlong token to a 32-character
  username prefix; the server parser slice must reject that whole overlong token
  rather than preserving the prefix match. This desktop-only fix does not change
  the deployed plaintext parser.
- `protocol/network.proto::ChatMessage.mention_ids` (field 8) and
  `server/src/storage/Records.hpp::MessageRecord.mentions` preserve individual
  user IDs only. `ChatServer::toProto` copies those IDs for replies, history,
  search and message events. Live broadcasts currently serialize once with
  viewer 0; adding viewer-dependent fields only to history would be incorrect.
- `Store::unreadSummary` counts non-self surviving messages after the acknowledged
  `(timestamp, id)` marker, with explicit plaintext-user mentions and independent
  encrypted-mention completeness. It must retain that meaning.
- `ClientState::messageJson` derives encrypted individual mentions locally.
  Its old word-boundary expression incorrectly accepts a prefix before `.` or
  `-`, and misses valid usernames ending in those characters. The prerequisite
  fix treats `[a-z0-9_.-]` as username continuation characters, case-insensitively,
  preserving the existing left boundary. A period directly after `@alice` is
  part of the username token, as in the plaintext resolver, rather than sentence
  punctuation. This changes notification eligibility, not cryptographic trust.
- `NotificationPolicy.hpp` distinguishes individual mentions from ordinary DM/
  group attention. Default personal-conversation attention must remain intact.
- `Permissions.hpp` has ordinary bits 0–19 and Administrator at 40. Role names
  are mutable display text; permissions are resolved through category/channel
  overrides. Neither raw text nor role names establish authorization.
- Discord imports store historical individual IDs and timestamps. Imports cannot
  recover the old role membership, sender permissions or everyone audience.
- Android currently consumes the existing protocol and has separate unfinished
  mention/activity navigation. Desktop acceptance cannot stand in for Android.

These are source findings. They do not establish new production observations.

## Semantics selected for implementation

An individual mention, a role mention and an everyone mention are separate
reasons for attention. One message may carry multiple reasons for the same user.
Self-authored messages never contribute unread attention or a notification.

Keep existing `@username` individual syntax and field 8 unchanged. Introduce
canonical plaintext tokens `@{role:123}` and `@{everyone}` on capable servers.
Role IDs are unsigned nonzero decimal IDs, never display names; reject overflow,
signs, whitespace and leading-zero alternate spellings. The composer inserts the
canonical token and renders a role's current display label. Copying plain text
preserves the canonical token. Bare `@everyone` continues to mean the individual
username `everyone`, if that user exists. Do not reserve a previously valid
username or silently convert old messages into a broadcast.

Explicit canonical tokens are recognized wherever ordinary mentions are currently
recognized, including quoted/code text. Changing Markdown escape/code semantics
is a separate compatibility decision. Deduplicate repeated targets. Accept at
most 50 distinct targets across all three kinds; reject excess with BadRequest
rather than silently delivering a partly classified broadcast. Malformed canonical
tokens are literal text; syntactically valid but nonexistent/cross-server targets
return NotFound without exposing foreign role metadata.

Only server text channels support role/everyone tokens. The target role must
belong to the channel's server, and the implicit default/everyone role is not a
role target. It uses the separate everyone kind. DM/group canonical broadcasts
are unsupported; plaintext requests using them return BadRequest. Within an
opaque encrypted body they remain literal text and cannot create trusted broad
attention. Existing encrypted individual-username mentions remain local only.

## Authorization and recipient snapshots

Add stable permission bits 20 (`MENTION_ROLES`) and 21 (`MENTION_EVERYONE`),
extending the named permission registry, `kAll`, explanation tests, permission
editors, CLI and protocol documentation together. Leave Guest, Member and
Moderator presets without the new bits. Owner/Administrator bypass follows the
existing resolver. Granting one bit never grants the other. Do not add a
mentionable-role flag in the first version.

For each send or edit, validate channel access, the existing action requirements,
and the author's effective channel permission for every requested broad kind.
Use category/channel/user overrides, not server-level role masks alone. A denied
broad token fails the entire operation before persistence or publication. A failed
edit leaves the previous content and classification intact. Existing individual
edit rights are preserved; adding broad targets requires their current permission.

Inside the same accepted-message transaction, snapshot eligible recipients:

- Individual: resolved individual users with ViewChannel at classification time.
- Role: current server members explicitly assigned the target role who have
  ViewChannel at classification time.
- Everyone: current server members with ViewChannel at classification time.

Persist a recipient's OR-combined three-bit reason mask, excluding the author.
Do not expand role/everyone users into `mention_ids`. Keep target IDs separately
for rendering. Subsequent membership, role or permission changes do not rewrite
historical classification, and newly joined users do not acquire old broadcasts.
Current ViewChannel/ReadHistory authorization still gates fetching summaries and
history; an old recipient row never grants access. Channel/message/account deletion
must clean up dependent rows transactionally.

An accepted edit replaces the content, targets and recipient snapshot atomically,
using current author permissions/membership. It may therefore change an unread
reason. Edits do not trigger desktop notifications or replay earlier delivery.
Deletion removes its reasons. Both invalidate/requery affected aggregate snapshots
through the existing recovery scheduler. An idempotent send replay returns the
original accepted result without reclassification or duplicate notification.

## Additive wire contract

Use capability `mentions.kinds.v1`; do not infer support from protocol minor,
unknown fields, display text or a generic notification setting. Allocate unused
Protobuf field numbers only when implementing against the then-current schema.
Never reuse/renumber existing fields.

Add an optional classification message to ChatMessage with a semantic version,
an explicit classified/unclassified state, deduplicated target descriptors and
the authenticated viewer's reason mask. An absent object means legacy/unknown;
a present classified object with mask zero means known nonrecipient. Unknown
versions, kinds or out-of-range masks are unavailable classification, never an
individual mention and never complete zero. Unknown cannot authorize a broad
notification. Replies, history, search, event replay/resume and live creates/edits
must produce the same projection for the same viewer and stored message revision.

Viewer-specific serialization is required: refactor persistent event storage and
fanout so each authenticated recipient receives only its own mask. Never publish
a viewer-0 zero mask as a classified result for all recipients. Persist canonical
message identity/revision and classification in a way that replay projects to the
reconnecting viewer. Retain immutable classification for every replayable event
revision; reading only the latest edited message row would rewrite an old create
event. Tests must cover both connected peers and resumed events.
Avoid putting the full expanded recipient list into message frames or IPC.

Keep `mention_ids` strictly individual and keep `read.summary.v1.mentions` strictly
explicit plaintext-user mentions. Old clients connected to a new server receive
individual mentions and ordinary messages as before; they do not gain broad
mention notification support. New clients on old servers retain that same
individual behavior and show broad-kind controls as unavailable. The composer
must explain unavailable broadcasts instead of suggesting that a canonical token
will notify people on a legacy server.

Add a separate `read.attention.v1` summary request/reply with the existing marker,
first/latest positions and event-sequence boundary. Bind classification totals to
that same SQLite snapshot. Return seven exact non-self message counts, one for
each nonzero recipient reason mask, plus an independent completeness flag.
Exclusive masks let any enabled-kind union be summed without double counting
messages that mention both a user and a role. Preserve decimal-string IPC totals,
saturation of legacy numeric projections, pacing, identity/generation validation
and recovery behavior. No body selection, membership recomputation, global cursor
advance or mutation of read markers is allowed while summarizing.

Historical/imported unclassified messages retain their known individual IDs;
role/everyone counts for those messages are unavailable, not fabricated zeros.
The new summary must distinguish known individual evidence from complete kind
classification. Encrypted bodies never receive server-classified explicit kinds;
DM/group ordinary attention remains exactly recoverable from envelope metadata,
while explicit individual completeness remains unavailable server-side.

## Device-local attention preferences

After server rollout and daemon consumption, add account/channel-scoped
`role_mentions_enabled` and `everyone_mentions_enabled`, default false. Individual
mentions retain their current behavior. Label both controls On this device and
explain legacy/unclassified/encrypted limitations. An unknown capability does not
erase saved choices; controls stay unavailable until an authenticated capable
connection is established. Persist via a checked transactional local migration,
acknowledging/broadcasting only after success. Extend GUI, CLI and IPC together.

Apply the current global disable, inaccessible channel, self, DND, focused
conversation, mute and None suppressors first. Global community/DM opt-outs remain
master switches. Then evaluate:

| Notification level | Community eligibility | DM/group eligibility |
| --- | --- | --- |
| Default | Individual OR enabled authoritative role/everyone reason | Existing ordinary conversation attention |
| Mentions | Same selected mention union | Locally decoded individual mention only |
| All | Every otherwise eligible new message | Every otherwise eligible new message |
| None | Never | Never |

Disabling a mention kind filters attention/badge projections, not total unread,
server markers or stored recovery metadata. All intentionally remains eligible
for ordinary messages even when broad kinds are disabled; explain this in the
control help. Turning switches on, reconnecting, editing, expiry or wake never
replays notifications. Aggregate and foreground/background decisions must use
preferences belonging to the originating account.

## Delivery and acceptance order

1. Finish the current encrypted-token prerequisite with unit and real TLS daemon
   coverage; install locally and verify package/runtime/data preservation.
2. Implement server migration, permission bits, parser/classifier and atomic
   storage. Test migration rollback, imported/legacy records, bounded inputs,
   overlapping kinds, cross-server roles, denied overrides, lost access, role
   deletion, author edits and idempotent replay.
3. Implement viewer-correct send/history/search/live/resume projections and
   `read.attention.v1`. Exercise two simultaneous viewers, equal timestamps,
   deleted markers, edits/deletes, no-marker and signed imported history, exact
   counts, stale sequence/identity replies, storage failures and large audiences.
4. Commit/push only the isolated server slice through the standing deployment
   workflow. Verify exact-SHA CI, published image provenance, live TLS pin,
   capability and an authenticated read-only result after rollout. Preserve the
   existing uncommitted desktop work. Recheck voice/sharing before disruption.
5. Implement daemon recovery, checked preference migration, policy and GUI/CLI.
   Test old/new peers in both directions, mixed/incomplete metadata, all levels,
   master opt-outs, overlaps, encrypted bodies, expiry, reconnect and colliding
   channel IDs across active/background accounts. Inspect compact/wide native
   dark/light keyboard/pointer behavior; package, install and compare artifacts,
   running binaries, identities, keys and preferences against a fresh backup.
6. Update both handoffs and durable memory with evidence. Independent human
   usability, physical Android and MLS/SFrame acceptance remain separate gates.

Phase 2D is open until the complete server-to-desktop policy path is deployed,
installed and validated. A contract or token-matching correction alone does not
complete the phase.

## Continuation validation evidence

The focused local run passed **62/62** unit tests across mention parsing and
classification validation, storage snapshots and rollback, permission resolution,
notification policy, unread recovery, local preference migration and sidebar
compatibility. The migration rollback fixture explicitly stores its certificate
pin before testing preservation; an earlier assertion incorrectly assumed that
`LocalStore::addAccount` persists an input pin.

The real TLS `MentionProtocol` suite passed **8/8**, including two connected
viewers, history/search projection, overlapping reason masks, denied edits,
membership snapshots, immutable resumed revisions, idempotent replay, canonical
DM rejection, foreign roles, target limits and overlong individual tokens. Its
real-daemon test exercises default-off device preferences, exact selected counts,
single delivery for overlapping reasons, edits and preference changes without
notification replay, ordinary All notifications and None suppression.

Logs for this run are `/tmp/omachat-mention-unit-final.log` and
`/tmp/omachat-mention-tls-final.log`; temporary logs are session evidence, not
production attestation. The handoff tracks durable copied evidence, server rollout,
local installation, native GUI checks and independent acceptance. Large-audience
load and every old/new peer, encrypted-race and deletion combination have not been
individually established by these focused suites.
