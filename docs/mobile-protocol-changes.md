# Additive mobile protocol changes

Protocol major/minor remain 1.2. Existing fields and enum values are preserved.
New clients must check advertised capabilities; older protobuf clients ignore new
fields. New servers continue accepting legacy SendMessage requests without IDs.
Database schema migration is 13→14; back up databases before normal upgrades.
Do not run an older server against a migrated database without its supported rollback.

## Durable messages: `messages.idempotency.v1`

`SendMessageRequest.operation_id` is field 7, a 16-byte client-generated identifier.
The server scopes it to the authenticated account's send-message operation. It
hashes the canonical request without the ID, including ciphertext and attachment
references. The accepted message and operation association commit in one SQLite
transaction. An identical retry returns the original serialized accepted message;
conflicting content returns ERROR_CONFLICT. Authorization is checked again, and
attachment claims are not repeated on an accepted retry.

Records are retained for the lifetime of the account, without a time expiry.
The retry horizon is therefore the account lifetime while authorization remains.
Deleted messages retain a digest/ID tombstone but erase the original response bytes;
retry returns ERROR_NOT_FOUND and cannot resurrect content. Account deletion cascades
operation records. Encrypted clients must persist and resend the original ciphertext,
not generate different ciphertext under the same ID. Android persists its private ciphertext and original operation ID before sending. Legacy servers require an uncertain-delivery state after an
ambiguous transport failure; automatic retries can duplicate messages.

## Read state: `read.markers.v1`

`ReadMarker` holds channel ID, message ID and server message timestamp.
`Envelope.set_read_marker`/`read_marker` use fields 157/158; `Event.read_marker`
uses 32; `SyncState.read_markers` uses 12. Updates require view/history permission
and a real message in that channel. The server advances by `(created_at, id)` order,
including imported history, not connection replay sequence. Markers are durable,
account scoped and delivered only to that account's devices. Desktop marks read
when the newest message is visible and the window focused; Android marks read only
for visible foreground conversation content. Background reception is not reading.

## Voice ownership: `voice.ownership.v1`

`VoiceState.owner_session_id` field 9 identifies the owning login session.
`JoinVoiceRequest.transfer` field 4 explicitly requests replacement. Without it,
joining from another session returns ERROR_CONFLICT with the transfer prompt.
A transfer replaces the relay stream/key, preserves server moderation flags and
publishes the new owner. Former owners cannot leave, mute, stream, watch or silently
rejoin the transferred lease. Disconnect cleanup captures the owning connection,
so delayed cleanup cannot terminate a later transfer. The desktop observes ownership
loss, shuts down media and clears automatic rejoin intent. Android now has an internal native UDP/packet/jitter transport foundation, tested
with explicit control joins/transfers. The application now integrates explicit joins/transfers, ownership-loss teardown and
a microphone foreground service. Internal Opus/audio and application ownership are
emulator-tested; physical audibility/routes/calls and screen-off/handover remain unverified.

## Server identity: `instance.identity.v1`

`HelloReply.instance_id` field 10 is a persistent random UUID in instance_settings.
It distinguishes a replaced server database at the same endpoint. It is not a TLS
identity or authentication credential. Android checks it before sending cached work;
a changed UUID clears local account entities and requires login. It is absent on
older servers, where this additional replacement detection is unavailable.

## Recovery and compatibility

SyncState describes current structure, not complete historical message reconciliation.
Android applies a complete replay of up to 32 events to its durable cached snapshot
and history; larger/unavailable replay triggers full sync and re-fetches selected-channel
history. The persisted cursor and event application share a Room transaction. Old-server live community
text was tested on protocol 1.2/v0.2.2 without the added capabilities. Current
server tests cover durable retry/conflict/deletion/restart, monotonic read markers,
and former-owner voice operations. Full hardware/media and E2E compatibility are
not established by these control tests.

## Login sessions: `sessions.manage.v1`

`Envelope.list_login_sessions`/`login_session_list`/`revoke_login_session` use
fields 159/160/161. This continuation does not change the database schema (14).
`ListLoginSessionsRequest.before_id` is an exclusive descending session-ID cursor;
0 starts at the newest. Replies include at most 100 unexpired sessions owned by
the authenticated account and `next_before_id` (0 when exhausted). Entries expose
ID, creation/refresh-expiry timestamps and observed connected/current flags.
No tokens, refresh digests, peer addresses or client-provided device names are exposed.
Connected means a ready control connection exists when the request is handled;
it is a snapshot, not a continuously updated presence indicator.

Revocation requires an authenticated account and a positive signed-64-bit session
ID. Missing and foreign IDs both return ERROR_NOT_FOUND. The account-scoped database
delete precedes the Ok acknowledgement; database errors return ERROR_INTERNAL.
All access grants for that login are invalidated, all its active control connections
close, and any voice lease it owns ends immediately. Other logins and encryption
keys are preserved. Revoked refresh and access tokens cannot refresh/resume.
Resume additionally checks the durable login row. Logout now uses the same complete
invalidation path, including grants created by earlier refresh rotations.

Self revocation is supported on the wire; the Android UI routes current-login
removal through Sign out, which also removes local account data and attempts key
revocation. Remote revocation does not erase cached data or keys on the other
installation; a new password/OAuth login can establish a new session. The list has
no push update event: refresh it to confirm an ambiguous response or external change.
Older servers do not advertise this capability; Android disables these controls.
