# omachat build and verification

## Requirements

CMake, Ninja, a C++ compiler, Qt 6 modules, protobuf, OpenSSL, SQLite, and the optional multimedia/voice development libraries. The complete package list and version details are maintained in [docs/development.md](docs/development.md).

## Setup

Install the documented build dependencies for your distribution. Do not put tokens or local secrets in this repository.

## Build and run

For a local desktop session that follows every push to remote `main`, launch
from the checkout:

```bash
./run-local.sh
```

Fetch-and-build is part of the clone. `scripts/dev-instance.sh start` is the
same entry point used by the omarchy menu. On each start it fetches
`origin/main`, fast-forwards local `main` when safe, then builds the current
client, daemon, and CLI when their source changed. It does not wait for a
release tag. It preserves the per-user theme at `~/.config/omachat/theme.toml`
and focuses the existing local window when already running. See
[docs/development.md](docs/development.md) for how local edits and remote
advances are handled, how to opt out for one launch, and [ARCHITECTURE.md](ARCHITECTURE.md)
for how future package and npm channels fit beside this source updater. If
startup work takes longer than a brief moment, a quiet themed splash appears
while the launcher checks, builds, and starts omachat.

For an ordinary one-off build without launching the desktop app:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

The installed GUI and CLI are `omachat` and `omachatctl`.

## Landing and initial review

`site/index.html` is served by `omlocal launch omachat-landing --open` from port 9143. The identity review is at `site/identity/proposals/` and is rendered from `creative/ombrand/brands/omachat/identity.json`.

## Checks

See `docs/development.md` for focused test and full-suite commands, and
`docs/STATUS.md` for checks actually run on the current changes. The landing
link check and static route checks are run as part of spoke setup.

## Product work tracked on GitHub

Feature plans and follow-up designs live in GitHub issues so implementation,
discussion, and acceptance criteria stay reviewable. The active roadmap is
maintained by [@HowieDuhzit](https://github.com/HowieDuhzit), the project's
main contributor.

| Area | Issue |
|---|---|
| GitHub integration with user attribution and multi-repo routing | [#4](https://github.com/Sleepy-Studio/OmaChat/issues/4) |
| Multi-forge routing and Omachat-associated Gitea exploration | [#5](https://github.com/Sleepy-Studio/OmaChat/issues/5) |
| Optional extensions, integrations, and plugins | [#6](https://github.com/Sleepy-Studio/OmaChat/issues/6) |
| Optional soundboard, libraries, and permissions | [#7](https://github.com/Sleepy-Studio/OmaChat/issues/7) |
| Configurable local event sounds and server sound policies | [#8](https://github.com/Sleepy-Studio/OmaChat/issues/8) |
| Voice-channel stage, sidechat, and multi-share viewing | [#9](https://github.com/Sleepy-Studio/OmaChat/issues/9) |
| Optional meme picker, creator, and shared collections | [#10](https://github.com/Sleepy-Studio/OmaChat/issues/10) |
| Optional image-generation API for profile avatars | [#11](https://github.com/Sleepy-Studio/OmaChat/issues/11) |
| @mention autocomplete, ranking, and personal shortcuts | [#12](https://github.com/Sleepy-Studio/OmaChat/issues/12) |
| Extensible message references, tags, links, and task actions | [#13](https://github.com/Sleepy-Studio/OmaChat/issues/13) |
| Expanded safe Markdown formatting | [#14](https://github.com/Sleepy-Studio/OmaChat/issues/14) |
| Local file search, pinned folders, palette action, and file-manager sharing | [#15](https://github.com/Sleepy-Studio/OmaChat/issues/15) |
| Optional local microphone self-monitor | [#16](https://github.com/Sleepy-Studio/OmaChat/issues/16) |
| Voice leave-on-close and stale-session recovery | [#17](https://github.com/Sleepy-Studio/OmaChat/issues/17) |
| Local mute-others and per-user voice mute controls | [#18](https://github.com/Sleepy-Studio/OmaChat/issues/18) |
| Alternating background treatment for consecutive author groups | [#19](https://github.com/Sleepy-Studio/OmaChat/issues/19) |
| Cross-workspace screen-share preview notification | [#20](https://github.com/Sleepy-Studio/OmaChat/issues/20) |

Issues #4–#20 are plans, not claims that the features are implemented. Check
each issue for current scope and status before starting related work. The
completed local updater and startup splash provenance is recorded in
[#3](https://github.com/Sleepy-Studio/OmaChat/issues/3).
