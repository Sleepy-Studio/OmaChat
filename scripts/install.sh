#!/usr/bin/env bash
# One-line OmaChat install for Arch Linux / Omarchy:
#
#   curl -fsSL https://raw.githubusercontent.com/Sleepy-Studio/OmaChat/main/scripts/install.sh | bash
#
# Installs build dependencies, builds the package with makepkg, and installs
# it with pacman. Safe to re-run to upgrade to the latest commit on main.
# For a specific release instead of the latest commit, set OMACHAT_REF, e.g.
#   OMACHAT_REF=v0.2.0 curl -fsSL .../install.sh | bash
#
# Quiet by default (a one-line status per step; full output only on
# failure). For full dependency/compiler/test output:
#   OMACHAT_VERBOSE=1 curl -fsSL .../install.sh | bash
set -euo pipefail

VERBOSE="${OMACHAT_VERBOSE:-0}"
for arg in "$@"; do
    case "$arg" in
        -v|--verbose) VERBOSE=1 ;;
    esac
done
export OMACHAT_VERBOSE="$VERBOSE"

# _run <description> -- <command...>: runs quietly, showing the captured
# output only if the command fails. With OMACHAT_VERBOSE=1, runs as-is.
_run() {
    local desc="$1"
    shift
    if [[ $VERBOSE == 1 ]]; then
        "$@"
        return
    fi
    local log
    log="$(mktemp)"
    if ! "$@" >"$log" 2>&1; then
        echo "omachat install: $desc failed:" >&2
        cat "$log" >&2
        rm -f "$log"
        exit 1
    fi
    rm -f "$log"
}

if [[ ! -f /etc/arch-release ]]; then
    echo "omachat install: this script is for Arch Linux / Omarchy only." >&2
    echo "For other distributions, self-host the server with Docker (see README)" >&2
    echo "or build from source: https://github.com/Sleepy-Studio/OmaChat#build" >&2
    exit 1
fi

if [[ $EUID -eq 0 ]]; then
    echo "omachat install: run this as your normal user, not root (it uses sudo where needed)." >&2
    exit 1
fi

REPO="https://github.com/Sleepy-Studio/OmaChat.git"
REF="${OMACHAT_REF:-main}"
WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

echo "omachat install: installing build dependencies (sudo password may be requested)"
_run "installing dependencies" sudo pacman -S --needed --noconfirm \
    base-devel git cmake ninja qt6-base qt6-declarative qt6-svg qt6-wayland \
    qtkeychain-qt6 protobuf abseil-cpp libsodium opus libpipewire openssl \
    tomlplusplus rnnoise ffmpeg

echo "omachat install: fetching $REF"
git clone --quiet --depth 1 --branch "$REF" "$REPO" "$WORKDIR/OmaChat" \
    || git clone --quiet "$REPO" "$WORKDIR/OmaChat"

echo "omachat install: building (this takes a few minutes; set OMACHAT_VERBOSE=1 for full output)"
cd "$WORKDIR/OmaChat/packaging/arch"
OMACHAT_SRC="$WORKDIR/OmaChat" BUILDDIR="$WORKDIR/build" OMACHAT_VERBOSE="$VERBOSE" makepkg -si --noconfirm

echo "omachat install: done. Run 'omachat' to start, or see the README for the CLI and self-hosting a server."
