# Forward secrecy and private media: implementation plan

Status: design and acceptance criteria, 2026-09-29. These features are **not
implemented**. Current direct/group messages use long-lived device-key
wrapping (`e2e.v1`); the media relay can decrypt voice and screen sharing.
Changing the UI label or rotating a transport key would not close either gap.

## Threat and compatibility requirements

- A later theft of a device's current secret must not decrypt messages or
  media recorded before the compromise. A group member removed at an epoch
  change must not decrypt traffic sent after that change. Newly added devices
  must not silently receive old history.
- The server must be able to route media and enforce membership without
  receiving media content keys. Packet type, stream, timing, size and the
  recipients remain visible to the server; this is not metadata privacy.
- Clients must bind a peer's new group credential to the existing verified
  device identity, show an identity change, and reject a silent downgrade to
  `e2e.v1` or transport-only media after an encrypted session is established.
- A protocol update needs capability negotiation and an explicit migration
  state. Old messages retain their original protection level; the new scheme
  cannot retroactively give them forward secrecy. Mixed-version participants
  may require an upgrade before joining an encrypted conversation or call.

## Conversation key protocol

Use an audited implementation of [Messaging Layer Security, RFC 9420](https://www.rfc-editor.org/rfc/rfc9420)
for direct and group conversations. MLS defines asynchronous group membership,
epoch transitions, forward secrecy and post-compromise recovery; its exported
secrets can also feed media keys. The existing `e2e.v1` custom envelope should
remain readable for historical messages, but new messages should enter a
versioned MLS group after all participants have migrated.

Implementation gates:

1. Select and package a maintained RFC 9420 library with a stable C++/C
   interface and acceptable licensing. Pin its version and test vectors.
2. Add server storage/relay for key packages, Welcome, proposals and commits.
   The server must not decide the group transcript or hold the group secret.
3. Bind MLS credentials to pinned OmaChat device keys and safety numbers.
   Persist per-device group state in encrypted local storage; a crash between
   sending a commit and storing the new epoch must recover without key reuse.
4. Migrate each DM/group independently. Block sends during an unresolved
   epoch change; handle offline devices, out-of-order messages, multi-device
   senders and member removal. Bound skipped-key and queued-message storage.
5. Test compromise of a current device key against recorded prior traffic,
   member removal, late join, reinstall, offline replay, server tampering,
   rollback, backup/restore and mixed-version peers. Review the resulting
   security claims before enabling the capability by default.

## Voice and screen sharing

Use [SFrame, RFC 9605](https://www.rfc-editor.org/rfc/rfc9605) to encrypt an
encoded Opus frame or H.264 access unit **before** the transport relay sees
it. Use sender-specific media keys derived from the current MLS epoch (or
distributed over that MLS group). Continue authenticating the UDP transport
hop by hop; the relay can still inspect routing metadata and forward opaque
content. A call must not start as "end-to-end encrypted" until every active
participant has authenticated the same key epoch.

Key and packet gates: unique sender key and counter per stream, no counter
reuse across restart, epoch rotation on join/leave and key compromise, bounded
replay windows, keyframe requests that reveal no content, and a fragmentation
budget under the 1400-byte datagram cap. Test audio loss/reordering, H.264
fragment loss, reconnect, two devices per user, a malicious relay, late
subscribers and a membership change during a share. Do not label media E2E
until the relay is shown unable to decode captured test packets.

## Screen sound and capture

Screen sound is opt-in by default in the current working tree. With it on,
PipeWire captures **all other applications' playback**, even when only one
window is shared. The [ScreenCast portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html)
returns a video stream and source type but no portable owning-application
identity or corresponding audio node. A safe per-application option therefore
needs a separate, explicit audio-source picker and an accurate live indicator;
it cannot be inferred from the window chosen in the portal.

Stereo sound needs a two-channel PipeWire capture and playback path, stereo
Opus encoding/decoding, a versioned stream marker, and a mono fallback for
older peers. Verify channel separation and voice mixing on real hardware.
DMA-BUF and VAAPI/QSV are performance work after privacy and two-machine
behavior are measured.
