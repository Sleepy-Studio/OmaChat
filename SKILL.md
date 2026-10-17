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

## Project-specific workflow

For IPC work, update `docs/protocol.md` and the corresponding client/daemon tests. For sibling integrations, use their documented CLI or local IPC contract and report whether live behavior was exercised.
