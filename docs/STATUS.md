# omachat status

Updated: 2026-10-17

## Current scope

omachat is an independently rooted repository containing the Qt desktop client, per-user daemon, CLI, self-hostable server, and Android client. Its desktop launch identity and packaged desktop entry use lowercase `omachat`. Local IPC exposes generic voice functions used by omachat and omavb; it has no Omkaon inbox, Discord bridge controls, Omkaon UI, or Omkaon-specific storage/API. Experimental Omachat QML panels are archived in the separate omkaon repository at `archive/omachat-ui/`.

The consolidated branch includes remote `main`, the local held worktree, and the two-commit standalone PR branch. Conflicting files were reconciled to retain independent features while removing duplicate IPC registrations and Omkaon-specific functionality. The prior lowercase historical status file is now `docs/development-history.md`; this file is the current status source.

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
- Source and documentation search for Omkaon references — no matches in the Omachat worktree outside generated build output.
- Android build and runtime, live external integrations, and visual review were not checked in this update.

## Repository snapshot

Before this status edit, `main` was at `22be6a6b9d236b314a0862d11d7533cb308cfcda` (`Merge remote main and consolidate local omachat work`). It included all commits from `origin/main`, plus 20 commits. The Omachat worktree was clean. No Omachat push had yet occurred.

The Omakaon archive was saved in its separate repository on local `main` as commit `ca67a34` (`Archive experimental omachat UI panels`); only the three `archive/omachat-ui/` files were committed there. Existing unrelated Omakaon worktree changes remain untouched.

## Next steps

Push Omachat `main` without force, fetch it again, and confirm local and remote `main` point to the same commit with a clean worktree. If the remote advances first, merge its new commits and resolve any conflicts while retaining functionality.
