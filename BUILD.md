# omachat build and verification

## Requirements

CMake, Ninja, a C++ compiler, Qt 6 modules, protobuf, OpenSSL, SQLite, and the optional multimedia/voice development libraries. The complete package list and version details are maintained in [docs/development.md](docs/development.md).

## Setup

Install the documented build dependencies for your distribution. Do not put tokens or local secrets in this repository.

## Build and run

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

The local development launcher is `scripts/dev-instance.sh start`; the installed GUI and CLI are `omachat` and `omachatctl`.

## Landing and initial review

`site/index.html` is served by `omlocal launch omachat-landing --open` from port 9143. The identity review is at `site/identity/proposals/` and is rendered from `creative/ombrand/brands/omachat/identity.json`.

## Checks

See `docs/development.md` for focused test and full-suite commands. For this enrollment, a CMake build and `ctest --test-dir /tmp/omachat-build --output-on-failure` were run; see `docs/STATUS.md` for outcomes. The landing link check and static route checks are run as part of spoke setup.
