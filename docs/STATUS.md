# omachat status

Updated: 2026-10-17

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

## Verification for this update

- `bash -n scripts/dev-instance.sh run-local.sh` — passed.
- Python syntax compilation of `scripts/startup-splash.py` — passed.
- `git diff --check` — passed.
- GTK 4.22 is available on this machine; the splash UI was not launched.
- No tests, build, fetch-and-launch, or desktop interaction was run.

## Repository snapshot

At the last fetch, `main` and `origin/main` both pointed to `59ceb93`. The
worktree has uncommitted updater, splash, and documentation changes. No commit
or push has been made for this update yet.

## Next steps

After these changes are pushed to `main`, a fresh clone gets the updater and
delayed themed splash directly from the repository. The next normal launch
fetches the remote tip and builds matching runtime binaries; subsequent
launches with no new push or source edit reuse that build and focus the existing
client.
