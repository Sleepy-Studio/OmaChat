# omachat agent instructions

Read [README.md](README.md), [IDEA.md](IDEA.md), [ARCHITECTURE.md](ARCHITECTURE.md), [BUILD.md](BUILD.md), [SKILL.md](SKILL.md), and [CAPABILITIES.md](CAPABILITIES.md) before substantial changes.

## Boundaries

- The ombrella spoke name is always `omachat` in source, docs, interfaces, updates, and handoffs.
- omachat owns its client, daemon, server, CLI, local data, and IPC contract. Integrate with sibling spokes only through documented CLI or IPC contracts.
- Never read, copy, stage, or commit credentials, `.env` files, private keys, or wallet data. The historical empty `.aws` marker is excluded from source control.
- Do not publish, push, deploy, contact external services, spend funds, or install/update system packages unless the user asks for that specific action.
- Keep `docs/STATUS.md` current after meaningful changes; record actual verification and Git state.
- Keep local voice/audio IPC opt-in and same-user; never expose it on the network.

## Desktop integrations

The omarchy menu entry runs `scripts/dev-instance.sh start`. The current Super+Enter binding opens a WezTerm tab in this checkout. omfigs owns the reusable binding template; update its source and use its reversible installer workflow for desktop policy changes.

## Local current-main launcher

- Keep `./run-local.sh` and `scripts/dev-instance.sh start` as the repo-owned local launch path. A fresh clone must get current-main syncing from these tracked files, without machine-specific updater setup.
- By default, every start fetches `origin/main`, fast-forwards only when safe, and builds the current client, daemon, and CLI. This follows all pushed commits; do not gate it on release tags.
- Preserve `~/.config/omachat/theme.toml`. Do not copy theme settings into the checkout or reset local edits.
- Keep lengthy startup work quiet and hands-off for ordinary users. The optional GTK splash is launched by the repo-owned entry point, delayed to avoid flashing on quick starts, and styled from the configured Omachat theme; a missing GTK runtime must not block launch.
- Local edits may be built when the checked-out branch still equals its fetched remote tip. If the remote advanced while edits are present, or local-only commits exist, default updating must stop safely and explain how to resolve the checkout.
- Support a per-launch opt-out with `OMACHAT_AUTO_UPDATE=0`; it builds the selected local checkout without fetching or moving Git refs. Keep auto-update enabled by default. Power users may override the remote, branch, build directory/type, dev runtime root, and theme file through documented `OMACHAT_*` environment variables.
- Keep the launcher single-instance: close previous local processes only when replacing a changed build, focus the existing client on repeat launches, and refuse to start alongside a packaged client that will not close.
- Document changes to sync, build-key, theme, and single-instance behavior in `SKILL.md`, `BUILD.md`, `docs/development.md`, and `docs/STATUS.md`.
- If adding package-manager, binary-release, or npm distribution, keep it as a separate explicit channel. It must not change the local check-out launch from all pushed `origin/main` commits or make release cadence a gate for that launch.
- For a shared rollback, push a forward `git revert` commit to `main`; do not force-reset the remote. A user can pin a local commit and opt out for one launch when they need to inspect a specific revision.
