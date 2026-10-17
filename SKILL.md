# omachat project skill

Use this guide when changing, reviewing, building, or integrating omachat. It supplements `AGENTS.md`.

## Before changing code

1. Read README, IDEA, ARCHITECTURE, BUILD, and CAPABILITIES.
2. Inspect `git status --short --branch` and the latest commit; preserve pre-existing local work.
3. Read [docs/protocol.md](docs/protocol.md) before changing IPC or network contracts.

## Working method

- Build using the documented CMake/Ninja flow; run only focused checks relevant to the change, then the project suite when appropriate.
- Keep voice event subscription opt-in, local, and same-user.
- Update integration contracts on both sides when an interface changes.
- Keep docs/STATUS.md dated and accurate about current checks, unrun checks, and observed Git state.
- Keep the landing dark-only; use the single footer ombrand link for identity studies.

## Local launch and updates

- Use `./run-local.sh` or the repo-owned `scripts/dev-instance.sh start` to launch the local app. The start path is enabled in the clone itself and fetches every commit from `origin/main` on each launch; it is not release-only.
- Fast-forward local `main` only when safe. Never reset local-only commits or overwrite local changes. Edits at the fetched remote tip are preserved and included in the build; if the remote advances while edits exist, stop for review.
- Rebuild the client, daemon, and CLI when the remote commit, source fingerprint, or runtime binaries change. Replace old local processes only for a changed build; focus the existing window on repeat launches.
- For a one-launch opt-out, `OMACHAT_AUTO_UPDATE=0 ./run-local.sh` builds the selected checkout without fetching or moving refs. Auto-update stays enabled by default. A shared rollback should be a forward `git revert` commit pushed to `main`; never force-reset remote history.
- Keep the per-user theme at `~/.config/omachat/theme.toml`; syncing and building must not modify it. Keep updater paths relative to the cloned repository so a new clone needs no machine-specific updater code.
- If launch work takes more than a brief moment, use the repo-owned optional GTK startup splash, styled from the configured Omachat theme. Delay showing it to avoid flashes, close it before the client opens, and keep launch functional when GTK is unavailable.
- Defaults should work without setup. Power users can override `OMACHAT_UPDATE_REMOTE`, `OMACHAT_UPDATE_BRANCH`, `OMACHAT_BUILD_DIR`, `OMACHAT_BUILD_TYPE`, `OMACHAT_DEV_ROOT`, and `OMACHAT_THEME_FILE` for a fork, branch, separate build/runtime directory, or alternate theme.
- When changing this workflow, update `AGENTS.md`, `BUILD.md`, `docs/development.md`, and `docs/STATUS.md` together.
- Treat future package-manager or binary releases as a separate, explicitly selected install/update channel. A future npm package can version JS tooling or libraries independently, but must not gate the local C++ app's all-pushes `main` update. If another updater backend is added, compose it behind an explicit channel choice and preserve the current local-main default.

## Project-specific workflow

For IPC work, update `docs/protocol.md` and the corresponding client/daemon tests. For sibling integrations, use their documented CLI or local IPC contract and report whether live behavior was exercised.
