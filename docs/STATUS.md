# omachat status

## Clean-checkout build repair — 2026-10-17

The first exact-SHA desktop CI run at 2e6134c fails in CMake configuration:
main already lists and instantiates VoiceboardDialog.qml, but its component is
absent from Git. The missing dialog is now included, using only the existing
voiceboard properties and controller method. Speaking still requires an explicit
button press; local monitoring defaults off. No bridge/audio action is executed
by this validation. A fresh full-client CMake configure passes after the repair;
full remote build/test results remain pending. This follow-up is separate from
the personal-status backend patch.

## Personal voice status server continuation — 2026-10-17

The server adds `voice.status.v1`, a separate SetVoiceStatusRequest and
VoiceState.status_text. Only the device that owns voice may change the status;
legacy mute/deafen preserves it. It is single-line plain text bounded to 80
Unicode scalars/320 UTF-8 bytes, cleared on leave/move, and retained on a same-room
device transfer. Existing channel audiences and sync snapshots carry the value.
No database migration, status history, microphone capture or new voice engine is
added. Desktop editor/display work is in a separate local continuation; this
server-only patch does not claim that main's GUI has that companion feature.

Three new real-TLS checks pass for peer delivery/snapshot and legacy mute,
validation/non-owner rejection, and ownership transfer/move/leave/rejoin.
A clean Release warnings-as-errors server/integration build passes. All 117 non-voice server integration checks pass; serial voice checks and
remote deployment verification are still pending at this local checkpoint. The patch is isolated from the existing
desktop dirty tree, based on 0887490. No unrelated launcher/layout change is
reverted.

Updated: 2026-10-17

## Product roadmap index

`BUILD.md` now indexes the current GitHub-tracked product plans (#4–#21) and
identifies [@HowieDuhzit](https://github.com/HowieDuhzit) as the main
contributor. The entries link to the issue acceptance criteria; they do not
claim those features are implemented. `CAPABILITIES.md`, `README.md`, and
`AGENTS.md` link to the same roadmap and keep planned work distinct from
implemented behavior. Every open issue from #4 through #21 now has a comment
with an ordered, end-to-end implementation sequence, gotchas, agent guidance,
and alternatives with a recommendation. The comments direct implementers to
execute all phases in sequence during one continuous work run, verify each
checkpoint, and continue without pausing for a separate trigger unless a
genuine external blocker prevents progress.

## Current behavior

omachat contains the Qt desktop client, per-user daemon, CLI, self-hostable
server, and Android client. The desktop app keeps its per-user theme at
`~/.config/omachat/theme.toml`.

Launching through the Omarchy menu or `./run-local.sh` now fetches every commit
on `origin/main`, fast-forwards local `main` when possible, and builds the
client, daemon, and CLI before starting. It rebuilds only when the remote has
advanced, the source/build marker differs, or a runtime binary is missing.
Uncommitted edits are preserved and included in the build if local `main` still
matches the fetched remote tip; if the remote advanced while edits are present,
launch stops for review. Local-only commits, fetch failures, and build failures
stop launch without resetting the checkout. A new build closes the old local
client and daemon before replacing them; repeat launches focus the already
running local client. The launcher keeps one active local client and daemon and
passes the existing theme path unchanged.
When launch work lasts beyond a short delay, a small optional GTK splash uses
the configured theme colors and closes before the client opens. Missing GTK
does not block launch.

The `stop` action stops the local instance and restores the packaged user
service. Startup refuses to open a local client if the packaged window does
not close, so both clients cannot remain open together. The packaged files and
the theme configuration are not changed by fetching or building.

## Entry points

- Desktop client: `omachat`; daemon: `omachatd`; CLI: `omachatctl`.
- Local current-main launcher: `./run-local.sh` and
  `scripts/dev-instance.sh start`.
- Build instructions: `docs/development.md`.
- Android project: `android/`.
- Local IPC contract: `docs/protocol.md`.

## Current documentation audit

The project-wide documentation review corrected stale README claims about
current checkout/test state, plugin availability, channel ID copying, voice
window-close behavior, and the planned avatar-generation integration. Historical
session and implementation handoffs are labeled as dated snapshots; their
recorded branch, deployment, and test results should not be read as current.

The roadmap index in `BUILD.md` covers open product issues #4–#21. It is linked
from `CAPABILITIES.md` and `README.md`; `AGENTS.md` asks future changes to keep
the index and current capability claims aligned, and to execute issue plans
continuously through their ordered phases. These issues are plans, not
implemented features.

## Verification and repository snapshot

- GitHub implementation-plan comments were added to all currently open issues
  #4–#21. These are planning comments; no feature implementation is claimed.
- The latest UI changes in `client/qml/views/MainView.qml` and
  `client/qml/views/ServerRail.qml` keep the channel column visible at narrow
  widths and add a collapsible server rail; they remain uncommitted.
- `git diff --check` — passed after the roadmap and UI updates.
- No build or tests were run for these documentation and QML changes.
- At inspection, `HEAD` and `origin/main` both point to `34b87e0`; the worktree
  contains the documentation updates and the uncommitted QML layout change.
