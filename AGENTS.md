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
