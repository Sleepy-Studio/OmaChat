# Voice and media

## Pipeline

```text
PipeWire capture (RT, 48 kHz mono f32, node.latency 480/48000)
  → SPSC ring → encoder thread:
      input gain → high-pass 80 Hz → RNNoise (optional) → AGC (optional)
      → VAD (RMS threshold + hangover) / push-to-talk / always
      → Opus (VoIP, 20 ms, 40 kbps default, in-band FEC)
      → ChaCha20-Poly1305 → UDP
  → omachat-server relay (authenticate, re-seal per recipient, never decode)
  → UDP → per-speaker adaptive jitter buffer
  → mixer thread: Opus decode / FEC / PLC → per-user gain → mix → soft clip
  → SPSC ring → PipeWire playback (RT)
```

Defaults: Opus, 48 kHz, mono, 20 ms frames, 40 kbps (configurable 24–96),
DSCP EF on the UDP socket. Deafen stops playback **and** transmission.

## Packet format

All integers network byte order (big-endian).

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | version (1) |
| 1 | 1 | type: 1 audio, 2 video, 3 control |
| 2 | 1 | flags: bit0 end-of-speech, bit1 keyframe |
| 3 | 1 | reserved (0) |
| 4 | 4 | stream id (assigned by the server) |
| 8 | 8 | sender user id (0 upstream; set by the server) |
| 16 | 4 | sequence (per stream and type) |
| 20 | 4 | timestamp (48 kHz samples) |
| 24 | n+16 | ChaCha20-Poly1305-IETF ciphertext + tag |

The 24-byte header is the AEAD associated data. Nonce:
`[direction][type][0][0][stream id:4][sequence:4]`. Each client session gets
its own random 32-byte key over the TLS control channel (`VoiceSession`), so
nonces never repeat: directions differ, stream ids are never reused within a
server process, and sequences only increase. Datagrams are capped at 1400
bytes.

Control packets (type 3) carry `Register` (binds the sender's UDP endpoint
to the stream; retried every 500 ms until `RegisterAck`), `Keepalive` (15 s,
keeps NAT bindings) and `KeyframeRequest` (reserved for screen sharing).

## Server relay

For each datagram: parse header → look up stream (unknown streams dropped
before any crypto) → session expiry → per-stream token bucket (audio 200/s
sustained, burst 400; normal voice is 50/s) → authenticate/decrypt → 64-packet
sliding replay window → forward. Audio goes to every other registered,
non-deafened stream in the same voice channel; muted, server-muted and
SPEAK-less streams are dropped at the relay. Video (reserved) goes only to
explicit subscribers.

## Jitter buffer

Per speaker, 64 slots, no allocation on the hot path.

- Start playout when buffered frames reach the target; target =
  `ceil((2·jitter + 5 ms)/20 ms)` + late-arrival bias, clamped to
  `[jitter_min_ms, jitter_max_ms]` (defaults 20–200 ms). Jitter is the
  RFC 3550 interarrival estimate.
- Missing frame: use the next packet's in-band FEC if present, otherwise
  Opus PLC. Never wait for retransmission.
- Late packets are dropped (and grow the target slightly).
- If the queue exceeds target + 3 frames it skips ahead, so latency cannot
  accumulate.
- End-of-speech flag or a stall ends the spurt and rebuffers.

## Measured latency

| Measurement | Result | How |
|---|---|---|
| Capture → audible output, software path | **40 ms** | `VoiceFixture.ToneTravelsThroughServerWithCorrectPitch`: two real daemons, real server relay over loopback UDP, simulated 20 ms device clock |
| Playback ring depth on real PipeWire | ~50 ms | `omachatctl voice stats` → `playback_buffer_ms` |

End-to-end mouth-to-ear adds the network RTT/2, the jitter target (20–60 ms
on a quiet LAN) and your devices' own buffering. Between two separate hosts
this has **not** been measured yet (see README → status).

## Devices

Devices come from the PipeWire registry (`Audio/Source`, `Audio/Source/Virtual`,
`Audio/Sink`); hot-plug is pushed to clients as `audio.devices`. A selected
device is passed as `target.object`; `default` follows the system default.
Changing devices reopens the streams. Echo cancellation is delegated to
PipeWire's `echo-cancel` module (select its source).

## Screen sharing

Not implemented in 0.1. The packet format, relay and daemon transport
already carry a separate video type with subscription-only forwarding and
keyframe requests so H.264 (NVENC/VAAPI/QSV/software) over
xdg-desktop-portal + PipeWire can be added without changing voice.
