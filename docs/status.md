# Development status

Handoff notes for whoever picks this up next (human or agent). Last updated
2026-09-27.

## Where things are

- Remote: `github.com/Sleepy-Studio/OmaChat` (private), branch `main`.
- Version string is still 0.1.0; protocol is **1.1** (1.0 + attachments),
  server schema **v2** (migrates v1 forward on start).
- 122 CTest tests pass (unit, integration, fuzz smoke). Zero compiler
  warnings in `build/`.
- Attachments were tested by hand in the GUI and work (picker, drag and
  drop, image previews, file cards, save, upload progress).

## Build, test, try it

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build -j8

scripts/dev-sandbox.sh start          # local server + you (howie) + bob, opens the GUI
scripts/dev-sandbox.sh bob message send general "hi" --attach ./file.png
scripts/dev-sandbox.sh stop           # kills it all, deletes the sandbox
```

The sandbox never touches `~/.config/omachat`, the keyring or the installed
package (`/usr/bin/omachat*` is an old 0.1 build without attachments).

## Done

Text chat, roles/overrides, DMs, search, voice over UDP relay (Opus,
PipeWire, VAD/PTT), reconnect/resume, CLI, Omarchy bar widget, Arch
packaging, and file attachments. See the README feature list and
`docs/protocol.md` for details.

## Not done (rough priority)

1. **Screen sharing**: portal ScreenCast + PipeWire capture + H.264
   (or VP8) over the existing UDP relay; `media.md` reserves a video packet
   type. Largest remaining feature.
2. **Voice between two separate machines** and real mouth-to-ear latency
   (software path ~40 ms + ~50 ms playback buffer, measured on one machine).
3. Group DMs; role and permission editing UI (works today via
   `omachatctl role …`); server-wide search.
4. More than one active account in the daemon at a time.
5. End-to-end encryption (the server can read messages and voice).

Smaller follow-ups from the attachment work:

- Pasting an image from the clipboard into the composer.
- Resuming an upload after a reconnect (today it restarts; uploads are tied
  to the connection by design).
- The Arch package (`packaging/arch/PKGBUILD`) has not been rebuilt since
  attachments landed; rebuild and run its `check()` before tagging.

## Decisions to keep

- **No ONCE / HTTP-only hosting.** ONCE only proxies HTTP on port 80; OmaChat
  needs TLS TCP 6473 + UDP 6474, and tunnelling voice over TCP costs
  latency. Deployment stays systemd + PKGBUILD.
- Attachments share the control connection (512 KiB chunks, four in flight)
  instead of a second port or HTTP endpoint.
- The GUI opens received files externally only when their content sniffs as
  image/audio/video/PDF/plain text; everything else is saved to Downloads.

## Gotchas

- The ASan build (`build-asan`, tests off) cannot run tests: Arch's
  system `libprotobuf` segfaults on its first serialize under ASan. Use the
  normal build for tests.
- Unix socket paths are capped at 107 bytes; long scratch directories break
  `--socket`.
- `pkill -f PATTERN` inside a shell whose own command line contains PATTERN
  kills that shell; kill by PID (the sandbox script keeps pid files).
