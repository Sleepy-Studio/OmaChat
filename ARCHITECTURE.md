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

## Decisions

Keep GUI and plugins dependent on the daemon's local public IPC contract; the daemon does not know about a particular plugin. Keep the application independent from ombrella runtime services.
