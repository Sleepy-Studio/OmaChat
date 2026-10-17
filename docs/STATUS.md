# omachat status

Updated: 2026-10-17

## Current scope

omachat is an independently rooted repository containing the Qt desktop client, per-user daemon, CLI, self-hostable server, and Android client. Its desktop launch identity and packaged desktop entry use lowercase `omachat`. Local IPC exposes generic voice functions used by omachat and omavb; it has no omkaon inbox, Discord bridge controls, omkaon UI, or omkaon-specific storage/API. Experimental Omachat QML panels are archived in the separate omkaon repository at `archive/omachat-ui/`.

The consolidated branch includes remote `main`, the local held worktree, and the two-commit standalone PR branch. Conflicting files were reconciled to retain independent features while removing duplicate IPC registrations and omkaon-specific functionality. The prior lowercase historical status file is now `docs/development-history.md`; this file is the current status source.

## Entry points

- Desktop client: `omachat`; daemon: `omachatd`; CLI: `omachatctl`.
- Android project: `android/`.
- Server and deployment: `server/`, `docs/self-hosting.md`, and `Dockerfile`.
- Build instructions: `docs/development.md`.
- Local IPC contract: `docs/protocol.md`.

## Verification for this update

- Fresh CMake configure and build of `omachat`, `omachatd`, and `omachatctl` — passed with server/tests disabled for that build.
- Restored the regular CMake configuration with server and tests enabled — configure passed. No tests were run during this consolidation.
- `desktop-file-validate packaging/desktop/org.omarchy.omachat.desktop` — passed.
- `git diff --check` and `git diff --cached --check` — passed before the consolidation commit.
- Local Markdown link audit found and fixed the development guide’s stale relative path to `scripts/dev-instance.sh`; ignored generated package staging files were excluded from the source audit.
- Implementation and product documentation search for omkaon integration references — no matches outside this status snapshot and generated build output.
- Android build and runtime, live external integrations, and visual review were not checked in this update.

## Repository snapshot

Before this status edit, local `main` and `origin/main` both pointed to `9f6cbb137885bbc31e823003bf3b924a92a85fff`, including all commits from the original remote tip. The Omachat worktree was clean.

The Omakaon archive was saved in its separate repository on local `main` as commit `ca67a34` (`Archive experimental omachat UI panels`); only the three `archive/omachat-ui/` files were committed there. Existing unrelated Omakaon worktree changes remain untouched.

## Next steps

Continue product work from the synchronized `main`. Recheck the Android build and runtime and the optional external integrations in their target environments when those are in scope.
