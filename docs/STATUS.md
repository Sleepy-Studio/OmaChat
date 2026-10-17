# omachat status

Updated: 2026-10-17

## Product roadmap index

`BUILD.md` now indexes the current GitHub-tracked product plans (#4–#20) and
identifies [@HowieDuhzit](https://github.com/HowieDuhzit) as the main
contributor. The entries link to the issue acceptance criteria; they do not
claim those features are implemented. `CAPABILITIES.md`, `README.md`, and
`AGENTS.md` link to the same roadmap and keep planned work distinct from
implemented behavior.

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

The roadmap index in `BUILD.md` covers open product issues #4–#20. It is linked
from `CAPABILITIES.md` and `README.md`; `AGENTS.md` asks future changes to keep
the index and current capability claims aligned. These issues are plans, not
implemented features.

## Verification and repository snapshot

- `git diff --check` — passed for this documentation-only update.
- No source changes, build, tests, launch, deployment, commit, or push were
  performed for this audit.
- At inspection, `HEAD` and the local `origin/main` tracking ref both point to
  `c0afee9` (`Add hands-off main updater to local launch`). The worktree has
  uncommitted documentation changes only.
