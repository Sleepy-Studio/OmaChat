# Direct voice calls

Capability `calls.direct.v1` adds person-to-person voice calls in an existing two-person DM. Group calls are outside this contract. Control signaling travels over the authenticated TLS connection; accepted calls reuse the existing Opus/encrypted UDP voice relay in the DM's private channel scope. This is the existing server-relayed voice encryption, not MLS/SFrame or end-to-end call encryption.

`StartDirectCallRequest(channel_id)` reserves both accounts and emits an ephemeral `DirectCall` event with `state=ringing`. No media stream or microphone capture is allocated at this point. `DirectCallActionRequest(call_id, action)` accepts `accept`, `decline`, `cancel`, and `hangup`. First acceptance selects the callee's owning connection; the originating connection owns the caller side. Only those two connections can request a media lease after acceptance or hang up an accepted call. Another logged-in device cannot transfer or steal the call. Any ringing recipient device may decline. The originating device alone may cancel.

A participant already in channel voice or another pending/accepted call produces a `busy` conflict. Calls to an offline or DND recipient fail explicitly. Ringing expires after 30 seconds. Decline, cancel, hang-up, owner disconnect, or the last recipient disconnect produces `state=ended` and a reason, removes both relay streams, and stops capture/playback. Calls never resume automatically after reconnect. Old ringing events are never replayed. Microphone/device startup failures terminate the call. Channel voice joins are rejected while either participant has a reserved direct call.

The daemon exposes:

- `call.start {channel, account?}`
- `call.accept|decline|cancel|hangup {call, account?}`
- `status.call` and `call.changed`, containing decimal-string `id`, `channel_id`, `caller_id`, `callee_id`, both owner session IDs, `account`, `state`, `reason`, `can_accept`, and `can_control`.

Account parameters prevent stale UI actions from affecting another account. Incoming calls for a background account appear in status and notifications with their original account. Accepting explicitly selects that account, provided local voice is idle. Switching away from an outgoing ringing call cancels it; switching during an active call hangs up.

Incoming ringing uses the existing notification chime immediately, then repeats every three seconds while eligible. Global notification enablement, DND, sound enablement, and configured sound volume apply. Every accepted/ended/disconnected state stops the repeat timer and pending sounds. Existing in-app and desktop notification switches remain independent. Notification click opens the originating conversation; answering still requires explicit acceptance.

CLI examples:

```sh
omachatctl call start DM_CHANNEL_ID
omachatctl call accept CALL_ID
omachatctl call decline CALL_ID
omachatctl call cancel CALL_ID
omachatctl call hangup CALL_ID
omachatctl mute
omachatctl deafen
```

`tests/integration/test_direct_calls.cpp` covers pre-acceptance capture inactivity, real Opus/AEAD relay audio after acceptance, mute/deafen commands, teardown/ringing cessation, decline/cancel/offline/busy/DND, stale account actions, timeout, multiple-device ownership, and forbidden direct-media leases. The RawClient-only `tests/integration/test_direct_call_protocol.cpp` separately validates server deployment signaling, owner-only media leases, teardown, offline handling, and real timeout without requiring new desktop daemon APIs. Human hearing, production routing/firewall acceptance, physical Android behavior, and MLS/SFrame verification remain separate acceptance work.
