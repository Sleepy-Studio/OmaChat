# omachat architecture

## Responsibilities

omachat owns a self-hostable chat and voice system: server, desktop GUI, daemon, CLI, local persistence, and local IPC. It does not own Discord access, ombrella project discovery, general retrieval, or model routing.

## Components and data flow

```text
omachat-server ⇄ TLS/protobuf ⇄ omachatd ⇄ same-user JSON IPC ⇄ GUI / omachatctl / plugins
```

The server persists shared state. `omachatd` holds the live client model and voice engine; the GUI and CLI use its local socket. Optional plugin integrations consume documented local APIs. Voice audio events are opt-in and delivered only to authorized same-user local IPC clients.

## Data and integrations

Local runtime state uses XDG directories; credentials are held through the configured keyring. The server database and deployment settings are described in [docs/self-hosting.md](docs/self-hosting.md) and [docs/security.md](docs/security.md). Integration contracts are linked from README and owned by each participant. omachat does not import sibling spoke internals.

## Landing and documentation

The dark landing is served from `site/` by omlocal route `omachat-landing` on port 9143. The separate identity review page is generated from ombrand's source package.

## Local update and distribution channels

The local checkout launch path is `./run-local.sh`, backed by
`scripts/dev-instance.sh start`. It fetches `origin/main` at every start and
builds the current client, daemon, and CLI when the remote commit, source
fingerprint, or runtime binaries change. This tracks every pushed commit; it
does not wait for a release. The per-user theme remains outside the checkout
at `~/.config/omachat/theme.toml`.
If launch work lasts longer than a short debounce, the launcher shows a small
theme-aware GTK splash until the daemon is ready, then closes it before opening
the client. The GTK helper is optional so a missing splash dependency never
blocks the local launch path.

This source-checkout path is distinct from installed distribution. Future
binary or package-manager releases can provide a stable install channel while
local checkouts continue following all pushes. A future npm package can publish
JavaScript tooling or libraries on its own version cadence. Neither channel is
implemented as a fallback for the local launcher. If more update sources are
added, keep them as composable backends with an explicit channel choice; do not
silently switch a local launch from `origin/main` to the latest release or let
release timing delay a pushed `main` commit.

For a one-launch local opt-out, `OMACHAT_AUTO_UPDATE=0` builds the checked-out
revision without fetching or moving Git refs. A shared rollback is a forward
revert commit on `main`, which the normal source updater can consume safely.

## Decisions

Keep GUI and plugins dependent on the daemon's local public IPC contract; the daemon does not know about a particular plugin. Keep the application independent from ombrella runtime services.
