# Personal voice status

Capability: `voice.status.v1`. This is a participant's temporary, server-visible
plain-text status, separate from account presence and the channel topic.

`SetVoiceStatusRequest { text }` is Envelope field 76. An empty or whitespace-only
string clears status. The server validates UTF-8, at most 320 bytes and 80 Unicode
scalars before trimming; controls, formatting characters, line/paragraph separators
are rejected. No HTML, Markdown, links or commands are interpreted.

Only the connection that owns the active voice session can set status. A caller
outside voice gets BadRequest; a second/previous device gets Conflict. No fields
in SetVoiceStateRequest change: legacy mute/deafen operations preserve status.
Accepted changes publish VoiceState.status_text (field 10) through the existing
channel audience and event sequence. SyncState snapshots include it. Duplicate
values are acknowledged without publishing another event. This creates no message,
read marker, notification, audio capture or extra voice engine.

Status lives in the server's in-memory voice record. It lasts until explicitly
cleared or that room is left, moved from, evicted or finally disconnected under
the existing voice-session lifecycle. A same-room rejoin/explicit device transfer
preserves it; a new room starts empty. Leave/rejoin and server restart do not
restore it. There is no timer or persisted status history. Resume follows the
existing retained voice lifetime; it creates no independent status lease.

The companion desktop continuation (not part of this server-only patch) exposes
`voice.status {text, account?, channel?}`. Optional account and
channel must match the active context; the GUI always supplies both captured when
opening the editor. Voice must be joined and not pending, and the server must
advertise the capability. The daemon forwards only after these checks and returns
the real server acknowledgement/error; it never invents local success.
`daemon.status.voice.status_text`, `voice.participants[].status_text`, snapshot
`voice_states[].status_text` and local `voice.state.status_text` expose the
server-owned value. The GUI renders it literally beside the correct participant
in the sidebar and floating window. The editor retains text after rejection;
success closes it. Closing the window still keeps voice connected.

CLI: `omachatctl voice status "BRB"`; `omachatctl voice clear-status`.
Older clients ignore the added VoiceState field. New clients disable editing on
older servers; they do not silently fall back to changing presence or channel topics.
Android's generated protobuf remains compatible, but Android status editing/display
is outside this desktop slice and has not been accepted on physical hardware.
