# omachat capabilities

## Implemented

| Capability | Evidence | Limits |
|---|---|---|
| Self-hosted TLS chat server with persistent storage and client protocol. | [server](server/), [protocol](protocol/network.proto), [self-hosting guide](docs/self-hosting.md). | Current deployment health is not checked by this inventory. |
| Desktop client, daemon, CLI, voice engine, and same-user local IPC. | [client](client/), [daemon](daemon/), [CLI](cli/), [IPC contract](docs/protocol.md). | Check [docs/STATUS.md](docs/STATUS.md) for current build/test evidence; live external integration behavior is environment-dependent. |
| Optional local voice event IPC extensions. | [IPC protocol](docs/protocol.md) and [voice integration](integrations/voicebox/). | Event delivery requires local opt-in and a running daemon. |
| Local desktop launch that fetches all pushes to `origin/main` by default, safely fast-forwards and rebuilds, and preserves the per-user theme. | [launcher](scripts/dev-instance.sh), [startup splash](scripts/startup-splash.py), [wrapper](run-local.sh), [launch guide](docs/development.md). | Runs at launch time; it does not update an already-running app in the background. A delayed optional themed splash covers slow startup work. Local-only commits block default updating; the one-launch opt-out can build the selected checkout. Power-user overrides are documented. |

## Planned

| Capability | Design or issue | Current state |
|---|---|---|
| Product roadmap: optional integrations and extensions, voice/media and cross-workspace notification improvements, richer messages and presentation, local file workflows, and profile/community features. | [GitHub issue index in BUILD.md](BUILD.md#product-work-tracked-on-github), issues #4–#21. | Planned only. Each open issue has scope, acceptance criteria, and an end-to-end implementation-plan comment. Execute its ordered phases continuously in one work run, verifying checkpoints as you go. |
