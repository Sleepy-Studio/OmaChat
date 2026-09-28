# Development status

Handoff notes for whoever picks this up next (human or agent). Last updated
2026-09-28.

## Where things are

- Remote: `github.com/Sleepy-Studio/OmaChat` (private), branch `main`.
- Version string is still 0.1.0. Protocol **1.2** (capabilities
  `search.server`, `dm.group`, `attachments.resume`, `video.h264`,
  `e2e.v1`; a 1.2 client still works with a 1.1 server). Server schema
  **v3**, daemon local schema **v2**; both migrate forward on start.
- 152 CTest tests pass (unit, integration, fuzz smoke), also inside the Arch
  package's `check()`. Zero compiler warnings in `build/`. `clang-format`
  is clean except `client/src/application/DaemonLink.hpp`, which predates
  this work.
- Everything since 0.1 is **uncommitted** in the working tree.

## Build, test, try it

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build -j8

scripts/dev-sandbox.sh start          # server + you (howie) + bob + carol, opens the GUI
scripts/dev-sandbox.sh bob voice join General
scripts/dev-sandbox.sh bob stream start           # bob shares a moving test pattern
scripts/dev-sandbox.sh bob dm howie "hi"          # an end-to-end encrypted DM
scripts/dev-sandbox.sh stop                       # kills it all, deletes the sandbox
```

bob and carol run with null audio and a synthetic screen; the GUI account
(howie) uses your real PipeWire devices. `OMACHAT_SANDBOX_GUI=0` starts
everything but the GUI. The sandbox never touches `~/.config/omachat`, the
keyring or the installed package.

## Done

Text chat, roles/overrides, DMs, voice over UDP relay (Opus, PipeWire,
VAD/PTT), reconnect/resume, CLI, Omarchy bar widget, Arch packaging, and:

- File attachments, pasting images/files with Ctrl+V, transfers that wait
  out a dropped connection and continue (uploads via `ResumeUpload`).
- Server-wide search (search panel scope switch; `message search --server`).
- Role and permission editor (server menu → Roles and members; channel menu
  → Permissions); CLI `role list|update|delete`, `override list|set`.
- Group conversations of 3–10 people (Home → +; CLI `group …`).
- Several accounts connected at once, one active (account button at the
  bottom of the server rail; `account switch`).
- Screen sharing: xdg-desktop-portal + PipeWire capture, H.264 via NVENC /
  AMF / x264, keyframe-on-demand over the relay, decoded in the daemon and
  shown from shared memory (voice panel monitor button; LIVE badge → click
  to watch; CLI `stream …`; Settings → Screen sharing). Carries other
  applications' sound (PipeWire links into a private capture node; OmaChat's
  own sound excluded), played to watchers only.
- End-to-end encryption of direct and group conversations, text and files,
  with key-change warnings and safety numbers (lock icon; CLI `e2e …`).
  Limits are in `docs/security.md`.
- `omachatctl dm USER TEXT` works (its `dm.send` daemon method was missing).

## Needs a human (the one-shot test pass)

None of these can be checked without hands, eyes or a second machine.
Start with `scripts/dev-sandbox.sh start` unless noted.

1. **Paste**: copy a screenshot, Ctrl+V in the composer → it becomes an
   attachment; copy text → still pastes as text; copy a file in the file
   manager → attaches it.
2. **Screen sharing, portal**: join General in the GUI, click the monitor
   button in the voice panel → the Hyprland picker appears → choose a
   screen, then a window. Stop it from the button and from the desktop's own
   "stop sharing" control. Watching your share needs a second account in
   voice (another machine, or a second GUI instance as bob).
3. **Watching**: `bob voice join General`, `bob stream start`; join General
   in the GUI, click bob's LIVE badge → the pattern appears above the chat
   and a 440 Hz tone plays (bob's synthetic "application sound"); maximize,
   close → the tone stops.
3b. **Shared sound, for real**: while sharing (step 2), play music or a
   video → the watcher hears it; talk on the call → the watcher does not
   hear your voice twice. Toggle Settings → Screen sharing → sound off.
4. **Encryption**: `bob dm howie "hi"` → lock icon in the DM header, message
   readable; open the lock → safety number equals
   `scripts/dev-sandbox.sh bob e2e safety howie`; mark verified. Send a file
   both ways in the DM.
5. **Group conversations**: Home → + → pick bob and carol → send; rename,
   add, leave from the right-click menu.
6. **Roles**: server menu → Roles and members: create a role, recolor, move
   it, give it to carol; channel menu → Permissions: deny carol Send
   Messages in #general and check `scripts/dev-sandbox.sh carol message
   send general x` fails.
7. **Several accounts**: account button → Add another account → log in as
   carol (password `testpass123`) → switch back and forth; while howie is
   active, `bob dm carol "ping"` → badge on the account button and a desktop
   notification naming carol's account.
8. **Resume**: send a large file and stop the server briefly mid-upload →
   the upload row says it continues when reconnected, then completes.
9. **Voice between two machines** and real mouth-to-ear latency (still
   untested; software path ~40 ms + ~50 ms playback buffer on one machine).
10. **Arch package**: `OMACHAT_SRC=$PWD/../.. makepkg -si` in
    `packaging/arch` (now depends on `ffmpeg`), restart `omachat.service`,
    repeat a few of the above on the installed build.

## Next

- Forward secrecy for encrypted conversations (a ratchet); end-to-end voice
  and screen sharing (would need group media keys).
- Stereo screen-share sound, and only the shared window's sound; DMA-BUF
  capture and VAAPI encoding.
- `pkgver` bump and a tag when cutting a release.

## Decisions to keep

- **No ONCE / HTTP-only hosting.** ONCE only proxies HTTP on port 80; OmaChat
  needs TLS TCP 6473 + UDP 6474, and tunnelling voice over TCP costs
  latency. Deployment stays systemd + PKGBUILD.
- Attachments share the control connection (512 KiB chunks, four in flight)
  instead of a second port or HTTP endpoint.
- The GUI opens received files externally only when their content sniffs as
  image/audio/video/PDF/plain text; everything else is saved to Downloads.
- Server channels are **not** end-to-end encrypted, by design (search,
  moderation, history for newcomers). Conversations always are when the
  server supports it; a participant without keys makes sending fail rather
  than silently falling back to plaintext.
- Video goes only to people who clicked to watch; decoded frames reach the
  GUI through a shared-memory file, never through the JSON IPC socket.
- Several accounts: IPC always describes the active account; background
  accounts only notify and count. Voice belongs to the active account.

## Gotchas

- The ASan build (`build-asan`, tests off) cannot run tests: Arch's
  system `libprotobuf` segfaults on its first serialize under ASan. Use the
  normal build for tests.
- Unix socket paths are capped at 107 bytes; long scratch directories break
  `--socket`.
- A makepkg (Release + LTO) build prints `-Wstringop-overflow` from
  protobuf's generated swap code in `network.pb.cc`. It is a GCC 16 false
  positive in generated/system code; the normal build is warning-free.
- `pkill -f PATTERN` inside a shell whose own command line contains PATTERN
  kills that shell; kill by PID (the sandbox script keeps pid files).
- `ClientState::clear()` resets by assignment; anything that is wiring
  rather than state (the E2E decryptor) must be carried across explicitly.
- Never drive the live desktop with `wtype`/`hyprctl` to test the GUI; use
  the offscreen GUI against a sandbox daemon (as carol/bob, null audio).
