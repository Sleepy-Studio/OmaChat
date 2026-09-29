# Development status

Handoff notes for whoever picks this up next (human or agent). Last updated
2026-09-29. "Current source" below means `main` at `6decdd4` plus the
changes in draft PR #1. The latest tagged release is older.

## Where things are

- Remote: `github.com/Sleepy-Studio/OmaChat` (**public** as of 2026-09-28), branch `main`.
- Latest tagged release: **0.2.0** (`v0.2.0`). The current source adds OAuth,
  profiles, server-side account deletion, Discord history import, inline
  media playback and custom server emoji after that tag. These changes need a
  new release; do not infer their presence from an installed package named
  `0.2.0-1` alone.
- Protocol **1.2** (capabilities
  `search.server`, `dm.group`, `attachments.resume`, `video.h264`,
  `e2e.v1`; a 1.2 client still works with a 1.1 server). Server schema
  **v7**, daemon local schema **v2**; both migrate forward on start. A database
  migrated to v7 cannot be opened by the v0.2.0 server.
- On 2026-09-29, the current source built and all
  **168 CTest tests passed**: 112 unit, 50 integration, 6 fuzz smoke.
  Integration tests require permission to bind loopback TCP/UDP sockets.
- GitHub CI for draft PR #1 passed build, unit, fuzz, integration, format,
  plugin and GUI screenshot checks on 2026-09-29. The PR adds the newly
  required `qt6-multimedia` package to CI, the Arch package and build
  instructions. The screenshot smoke test now uses Xvfb and a writable
  temporary directory; the earlier screenshot-mode changes are already on
  `main`. These fixes have not been merged into `main` yet.
- The manual test pass below was last done in the sandbox on 2026-09-28;
  newer OAuth, profile, account deletion, and import paths need live checks.
- A real `omachat-server` was also stood up on this machine (systemd,
  self-signed cert, `/var/lib/omachat`) and a real client connected,
  registered and sent a message through it — the first non-sandbox,
  non-test-fixture proof the whole stack works.
- Install is now one command for everyone: `scripts/install.sh` (curl|bash)
  builds and installs the Arch package; a Docker image
  (`ghcr.io/sleepy-studio/omachat-server`, built from the repo-root
  `Dockerfile`) self-hosts a server with zero manual config — both verified
  end to end (clean clone/build/install; container generates its cert and
  config, a real client registered and sent a message through it). AUR
  publishing needs a one-time manual step only the repo owner can do — see
  `docs/aur-publishing.md`; the AUR package was published for v0.2.0.
- Project notes dated 2026-09-29 report that `43a145e` was locally installed
  and the Coolify server redeployed (server schema 5→6, daemon connected).
  No live Discord history has been imported. Deployment health is volatile;
  recheck it before relying on it.

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
- OAuth sign-in and account linking for configured Discord and GitHub
  providers, profile sync, editable member profiles, and server-side account
  deletion. Google sign-in is temporarily hidden in the GUI.
- DiscordChatExporter JSON channel history import in Add Server and an
  offline operator tool for existing servers. Both preserve historical dates
  and avoid duplicating already imported Discord message IDs. See
  `docs/discord-migration.md` for data and format limits.
- Inline audio/video attachment playback, Unicode emoji picker and per-server
  custom emoji are in `6decdd4`; these newer paths still need manual checks.
- Screen sound is off by default in the current working tree. The GUI
  setting requires confirmation on each share, and CLI `stream start --audio`
  explicitly opts into capturing every other application's playback; this
  is not per-window audio. A bare CLI start stays silent with old configs.

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
3. **Watching**: `bob voice join General`, `bob stream start --audio`; join General
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
    repeat a few of the above on the installed build. (Done 2026-09-28 for
    0.2.0: `makepkg -si` installed clean, `check()` passed, real daemon
    served a real login through a real self-hosted server.)
11. **GHCR pull:** Coolify deployed the published image according to the
    2026-09-29 project notes. An unauthenticated fresh pull has not been
    rechecked in this status pass; do that before advertising the Docker
    command to new hosts.
12. ~~**AUR**~~ — done 2026-09-28: https://aur.archlinux.org/packages/omachat
    is live (`omachat 0.2.0`). Future releases: see "Every future release"
    in `docs/aur-publishing.md`.
13. **New paths:** verify Discord and GitHub OAuth linking and profile
    refresh against a live provider; import a representative Discord export
    into a new server and inspect channels, replies, authors, dates and
    attachments; delete a disposable server account and inspect remote data.

## Next release plan

**0.3.0 candidate (current feature set):** land and verify the CI dependency
and GUI smoke fixes on GitHub; rerun the clean package check; perform the
new-path live tests
above plus a two-machine voice/screen-share pass; update the README and
protocol/security/media docs to describe the shipped source; tag only after
the checks pass. Build and publish the matching Docker image and AUR update,
then verify fresh installs. No tag, image release, or AUR update is claimed
for this working tree.

**Security/media protocol release after 0.3.0:** forward secrecy for direct
and group conversations and end-to-end voice/screen sharing remain open.
Use a reviewed group key protocol and authenticated frame encryption;
key epochs, membership changes, multi-device history, downgrade prevention,
and server visibility all need tests before claiming these properties.
`docs/security-roadmap.md` records the design and acceptance gates. Stereo
screen sound and selecting one application's sound also remain open. The
ScreenCast portal does not identify the application behind a selected window,
so per-window sound cannot be inferred from the video choice.

**Other media work:** DMA-BUF capture and VAAPI/QSV encoding, after the
two-machine baseline is measured.

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
