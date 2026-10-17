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
