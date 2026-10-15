# Unread summary protocol

`read.summary.v1` is an additive Hello capability. Clients must check it before
sending `GetUnreadSummaryRequest` (Envelope 163); the response is `UnreadSummary`
(Envelope 164). Servers without it retain the bounded history-recovery fallback.
Existing clients, read markers, history requests and database schema are unchanged.

Each request names one message channel. The authenticated user must have both
ViewChannel and ReadHistory; inaccessible and nonexistent channels return
NOT_FOUND. Voice/category channels return BAD_REQUEST. Requests share the history
rate bucket. A storage error returns INTERNAL, never an empty success.

The response is a single database snapshot:

- `marker` always names the requested channel. Its zero position means no saved
  marker. A saved marker retains its timestamp/ID even if its message was deleted.
- `count` counts surviving non-self messages strictly after the saved marker in
  ascending `(created_at, message_id)` order. Without a marker, all surviving
  non-self messages count, including imported history. There is no page cutoff.
- `mentions` counts unread plaintext messages whose stored explicit user-mention
  IDs contain the reader. Multiple mentions in one message count once; edits and
  deletions are reflected. It does not classify role/everyone mentions.
- Encrypted direct/group messages count as unread using envelope metadata only.
  They contribute no server-classified mentions. `mentions_complete` is false
  whenever any unread encrypted message exists; clients must not present that
  mention count as exhaustive. No ciphertext or plaintext body is returned.
- `first` is the earliest unread non-self position; it is absent when count is zero.
  `latest` is the newest surviving position including self messages, or absent in
  an empty channel. A captured latest position can later be submitted to the
  existing marker API; deletion before acknowledgement can still yield NOT_FOUND.
- `sequence` is the last persistent event sequence at this snapshot. The server
  handles SQL, mutations and persistent publication synchronously on one event-loop
  thread, so no event interleaves the snapshot and boundary capture.

Client reconciliation must retain events newer than the response boundary and
apply each exactly once. Do not advance a global event cursor to this boundary:
other channels may still have older queued events to consume. Summary requests
are read-only and do not publish events or acknowledge messages. Discard pending
summaries on account/connection changes, reconnect or resync: sequences are
process-local and can restart. Access changes invalidate cached summaries.

The endpoint supplies authoritative snapshots. Integrating summaries with daemon
live-event tracking and implementing channel/category mark-read, retry,
first-unread navigation and unread-only filtering are separate acceptance work.
