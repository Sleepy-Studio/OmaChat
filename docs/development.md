# Local development against a real server

How to run a **local build** while the packaged `omachat` is installed — same
account, same hosted server — without replacing `/usr/bin` or disturbing
pacman's ownership of it.

## Quick start

```bash
./run-local.sh              # build, then start the local daemon + GUI
./run-local.sh check        # assert it is on the same account as the package
./run-local.sh stop         # stop local, bring the packaged service back
./run-local.sh status       # what is running, and as whom
```

Edit, re-run `./run-local.sh`, see the change. A loop is a rebuild
(usually seconds) plus one command.

Under the hood `./run-local.sh` builds and delegates to
[`scripts/dev-instance.sh`](../../scripts/dev-instance.sh), which also has
finer-grained subcommands:

| Command | Effect |
|---|---|
| `scripts/dev-instance.sh start` | stop packaged, start local daemon + GUI |
| `scripts/dev-instance.sh daemon` | local daemon only, no GUI |
| `scripts/dev-instance.sh client` | local GUI only (daemon must be up) |
| `scripts/dev-instance.sh stop` | stop local, restart packaged service |
| `scripts/dev-instance.sh status` | what is running |
| `scripts/dev-instance.sh check` | assert same account as the package |

## Why two environment variables

A local client and the packaged one collide on two resources. Both are
relocated:

- **`OMACHAT_SOCKET`** — moves the daemon socket
  (`shared/src/omachat/core/Paths.cpp:63`).
- **`XDG_RUNTIME_DIR`** — `runtimeDir()` is where the client keeps its
  single-instance lock, `<runtimeDir>/gui.sock`
  (`client/src/platform/SingleInstance.cpp:12`). Without this a local build
  forwards to the running packaged window and exits without drawing.

Relocating `XDG_RUNTIME_DIR` has a second-order effect: **Qt resolves the
Wayland socket as `$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY`.** Override the
variable alone and the client falls back to X11, failing with
`cannot open display :0`. `ensure_runtime()` therefore symlinks the real
compositor sockets (`wayland-*`) into the dev runtime directory.

## What is shared, and what is not

Shared, and therefore unaffected by which build you run:

- the account database (`~/.local/share/omachat/omachat.db`) — holds accounts,
  pinned certificates, per-user volume, muted channels
- the Secret Service keyring (session tokens)
- `~/.config/omachat/config.toml`
- the server itself: a local build logs into the **same hosted server** as the
  installed package

Only the **daemon** and **client** binaries are swapped.

All dev state lives in `/tmp/omachat-dev` (override with `OMACHAT_DEV_ROOT`):

```
/tmp/omachat-dev/
├── omachat.sock          # the dev daemon's socket
├── daemon.pid
├── daemon.log            # daemon output
├── client.log            # client output
└── run/                  # private XDG_RUNTIME_DIR for the dev client
```

Only **one daemon runs at a time**: `start` stops the packaged
`omachat.service` first. Stopping it does not log you out — the session
lives in the database, not the process. SQLite is in WAL mode, so a shared
database is safe regardless.

## Verifying which build you are running

```bash
./run-local.sh check
```

Prints the local daemon's live account and connection state next to the
packaged one. `status` only names the server once a channel is active, so the
check keys on **account + connection state**, not the server line. When the
packaged daemon is stopped — as it is while the dev daemon runs — the check
reports that no live comparison was possible instead of claiming a mismatch.

To confirm the binary itself, from another shell:

```bash
readlink -f /proc/$(cat /tmp/omachat-dev/daemon.pid)/exe
# -> .../omachat/build/daemon/omachatd

pacman -Qkk omachat      # packaged install: "0 altered files"
```

## Promoting a build to the installed package

When a change is good, replace `/usr/bin` properly rather than leaving it to a
symlink:

```bash
cd packaging/arch
OMACHAT_SRC=$PWD/../.. makepkg -si
```

`OMACHAT_SRC` makes the PKGBUILD build your working tree instead of the
published tag (`packaging/arch/PKGBUILD`). It runs the full test suite, so it
is a slower, deliberate step — not an edit-compile loop.

Avoid symlinking `/usr/bin/omachat` at the build tree: pacman stops owning the
path, `pacman -Qkk` reports it modified, and the next upgrade overwrites it.

## Audio note

`omachatd` registers PipeWire nodes on startup. If the client logs
`Failed to connect to pipewire instance "Host is down"`, PipeWire was not
ready:

```bash
systemctl --user restart pipewire pipewire-pulse wireplumber
```

A *silent* microphone (a source that exists but captures only zeros) is a
different problem — hardware, ALSA routing, or a missing jack — not something
a restart fixes. Check with:

```bash
pactl list short sources   # an alsa_input.* source should exist
parec --device=alsa_input.… --file-format=wav out.wav   # then inspect levels
```

## Offline testing

To test without touching any hosted server, use the dev sandbox. It runs a
server on `127.0.0.1:26473` plus `bob` and `carol` daemons with null audio,
and never touches your real config, keyring or package:

```bash
scripts/dev-sandbox.sh start
scripts/dev-sandbox.sh stop
```

## Upstream status

`run-local.sh` and `scripts/dev-instance.sh` are **not yet upstreamed** — they
are committed locally only.

They are a candidate for a pull request: self-contained, no dependencies beyond
the tools already required to build, and the two environment overrides they
rely on are documented stable behavior (`OMACHAT_SOCKET`, `XDG_RUNTIME_DIR`).
Before proposing them, review whether the hardcoded `OMACHAT_DEV_ROOT` default
of `/tmp/omachat-dev`, and the local-machine assumptions in `check`, belong in
an upstream patch or should be parameterized.