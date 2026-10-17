# omachat capabilities

## Implemented

| Capability | Evidence | Limits |
|---|---|---|
| Self-hosted TLS chat server with persistent storage and client protocol. | [server](server/), [protocol](protocol/network.proto), [self-hosting guide](docs/self-hosting.md). | Current deployment health is not checked by this inventory. |
| Desktop client, daemon, CLI, voice engine, and same-user local IPC. | [client](client/), [daemon](daemon/), [CLI](cli/), [IPC contract](docs/protocol.md). | The enrollment build and CTest suite passed; live external integration behavior is not covered by that suite. |
| Optional local voice event and inbox IPC extensions. | [IPC protocol](docs/protocol.md) and [voice integration](integrations/voicebox/). | Event delivery requires local opt-in and a running daemon. |

## Planned

| Capability | Design or issue | Current state |
|---|---|---|
| None. | — | No additional planned capability is recorded here. |
