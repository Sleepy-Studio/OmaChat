# Idea: omachat

## Original project prompt

> “okk it should be in ombrella and omachat case and caught up to ombrella scaffold standards and indexed in omnoda, omrag, make its landing, etc. its a spoke”

## Interpreted goal

Bring the existing omachat desktop chat and voice platform into the ombrella workspace as a lowercase-named, independently runnable spoke. Preserve its existing repository and work while adding the standard spoke documentation, local landing and identity review, workspace maps, and local knowledge indexing.

## Initial scope

- First useful outcome: keep the Qt client, daemon, CLI, and server usable from `omachat/` and document their contracts.
- Expected users and environment: local ombrella desktop users and self-hosters.
- Main workflows: chat, voice, local IPC automation, and optional integrations.
- Constraints: preserve the standalone build; keep credentials private; document runtime integrations and verification accurately.

## Initial presentation

- Landing: `site/index.html`, registered as `omachat-landing` on port 9143.
- Identity review: `site/identity/proposals/index.html`, generated from ombrand source. Three proposals are studies; none is adopted.
- Status: `docs/STATUS.md` records the current implementation and repository snapshot.

## Open questions

- Whether the current local feature work should be split into separate upstream review units before publication.
