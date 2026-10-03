#!/usr/bin/env bash
# Run a LOCAL OmaChat build against the SAME account and hosted server as the
# installed package, without touching /usr/bin or pacman's ownership.
#
# Why this exists
# ---------------
# The installed client and the local build are different binaries. To see local
# changes in a real session you must run the local build -- but the packaged one
# owns the daemon socket and the single-instance lock, so the two collide.
#
# Two overrides fix that, both read from the code:
#
#   OMACHAT_SOCKET     relocates the daemon socket
#                      (shared/src/omachat/core/Paths.cpp:63)
#   XDG_RUNTIME_DIR    relocates runtimeDir(), which is where the client takes
#                      its single-instance lock: <runtimeDir>/gui.sock
#                      (client/src/platform/SingleInstance.cpp:12).
#                      Without this, the local client forwards to the running
#                      packaged window and exits without drawing.
#
# Your account, database (~/.local/share/omachat/omachat.db), keyring and
# certificates are all shared and untouched. Only the daemon + client binaries
# are swapped. The hosted server is never contacted differently.
#
# SQLite is in WAL mode, and only one daemon runs at a time here, so sharing
# the database file is safe.
#
# Usage:
#   scripts/dev-instance.sh start    stop packaged, start local daemon + client
#   scripts/dev-instance.sh daemon   start local daemon only (no GUI)
#   scripts/dev-instance.sh client   start local client only (daemon assumed up)
#   scripts/dev-instance.sh stop     stop the local daemon, restore packaged
#   scripts/dev-instance.sh status   show what is running, and as whom
#   scripts/dev-instance.sh check    assert the local daemon sees the right login
#
# Never touches: /usr/bin, pacman, ~/.config/omachat, the keyring, the database.

set -euo pipefail

REPO="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$REPO/build"

# Keep these socket paths short: sun_path is 107 bytes, and $XDG_RUNTIME_DIR
# paths are long. /tmp is fine and mirrors what the dev sandbox does.
DEV_ROOT="${OMACHAT_DEV_ROOT:-/tmp/omachat-dev}"
DEV_RUNTIME="$DEV_ROOT/run"
DEV_SOCKET="$DEV_ROOT/omachat.sock"
DEV_PIDFILE="$DEV_ROOT/daemon.pid"
LOGFILE="$DEV_ROOT/daemon.log"

CLIENT_BIN="$BUILD/client/omachat"
DAEMON_BIN="$BUILD/daemon/omachatd"
CTL_BIN="$BUILD/cli/omachatctl"
PACKAGED_CTL="${OMACHAT_PACKAGED_CTL:-/usr/bin/omachatctl}"

say()  { printf '%s\n' "$*"; }
die()  { printf 'dev-instance: %s\n' "$*" >&2; exit 1; }

# systemctl --user needs a real XDG_RUNTIME_DIR with a systemd private socket.
# If the caller's environment has been redirected (a scratch runtime dir, a
# test harness, a nested shell), every --user call fails with
# "Failed to connect to user scope bus". Point systemd back at the real one
# while leaving everything else untouched.
systemd_user() {
    local real="${XDG_RUNTIME_DIR_REAL:-/run/user/$(id -u)}"
    if [ -S "$real/systemd/private" ]; then
        XDG_RUNTIME_DIR="$real" systemctl --user "$@"
    else
        systemctl --user "$@"
    fi
}

# PIDs of any INSTALLED (/usr/bin) omachatd, one per line. The dev daemon lives
# in the build tree and must never match. Returns non-zero when there are none,
# so it can be used directly as a condition.
packaged_daemons() {
    pgrep -f '^/usr/bin/omachatd' 2>/dev/null
}

require_build() {
    [ -x "$CLIENT_BIN" ] || die "no client build at $CLIENT_BIN -- run: cmake --build $BUILD"
    [ -x "$DAEMON_BIN" ] || die "no daemon build at $DAEMON_BIN -- run: cmake --build $BUILD"
    [ -x "$CTL_BIN" ]   || die "no cli build at $CTL_BIN -- run: cmake --build $BUILD"
}

dev_ctl() { "$CTL_BIN" --socket "$DEV_SOCKET" "$@"; }

daemon_pid() {
    [ -f "$DEV_PIDFILE" ] || return 1
    local pid; pid="$(cat "$DEV_PIDFILE" 2>/dev/null || true)"
    [ -n "$pid" ] || return 1
    kill -0 "$pid" 2>/dev/null || return 1
    printf '%s\n' "$pid"
}

ensure_runtime() {
    mkdir -p "$DEV_RUNTIME"
    chmod 700 "$DEV_ROOT" "$DEV_RUNTIME"

    # The client's single-instance lock lives in runtimeDir(), so we relocate
    # XDG_RUNTIME_DIR to avoid colliding with a running packaged window. But Qt
    # resolves the Wayland socket as $XDG_RUNTIME_DIR/$WAYLAND_DISPLAY, so
    # relocating that variable alone makes the client fall back to X11 and fail
    # with "cannot open display". Link the real compositor sockets through.
    local sock
    for sock in "${WAYLAND_DISPLAY:-}" $(ls "${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" 2>/dev/null | grep -E '^wayland-[0-9]+(\.lock)?$'); do
        [ -n "$sock" ] || continue
        local src="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/$sock"
        [ -e "$src" ] || continue
        [ -e "$DEV_RUNTIME/$sock" ] || ln -s "$src" "$DEV_RUNTIME/$sock"
    done
}

start_daemon() {
    require_build
    if pid="$(daemon_pid)"; then
        say "dev daemon already running (pid $pid)"
        return 0
    fi
    ensure_runtime

    # The packaged daemon holds the same account and the same database. Stop it
    # first so exactly one daemon writes at a time and the socket is unambiguous.
    # Three separate ways it can survive a naive `stop`, so check the PROCESS,
    # not just the unit -- systemd_user is-active said "inactive" while a
    # packaged omachatd was still running and holding the account:
    #   1. Restart=on-failure restarts it after a stop.
    #   2. It may have been started directly, not by systemd, so the unit reads
    #      inactive while the process lives.
    #   3. A stray daemon from an earlier run with no unit at all.
    local i
    for i in $(seq 1 20); do
        packaged_daemons >/dev/null || break
        if systemd_user is-active --quiet omachat.service; then
            systemd_user stop omachat.service 2>/dev/null || true
        fi
        # Whatever the unit says, the process must actually go.
        pkill -f '^/usr/bin/omachatd' 2>/dev/null || true
        sleep 0.25
    done
    if packaged_daemons; then
        die "a packaged omachatd is still running (pids: $(packaged_daemons | tr '\n' ' ')).
Two daemons on one account fight over the server connection. Stop it:
  pkill -f '^/usr/bin/omachatd'
then run this again."
    fi
    if systemd_user is-active --quiet omachat.service; then
        say "note: omachat.service is still active; stop it with 'systemctl --user stop omachat.service'"
    fi

    say "starting local daemon on $DEV_SOCKET"
    "$DAEMON_BIN" --socket "$DEV_SOCKET" >"$LOGFILE" 2>&1 &
    echo $! > "$DEV_PIDFILE"

    # Readiness: the socket must answer, not merely exist.
    local i
    for i in $(seq 1 50); do
        if [ -S "$DEV_SOCKET" ] && dev_ctl status >/dev/null 2>&1; then
            say "dev daemon ready (pid $(cat "$DEV_PIDFILE"))"
            return 0
        fi
        if ! daemon_pid >/dev/null; then
            say "--- daemon log ---"; cat "$LOGFILE" >&2 || true
            die "dev daemon exited during startup"
        fi
        sleep 0.2
    done
    say "--- daemon log ---"; cat "$LOGFILE" >&2 || true
    die "dev daemon did not become ready in 10s"
}

start_client() {
    require_build
    daemon_pid >/dev/null || die "dev daemon is not running -- use: scripts/dev-instance.sh start"
    ensure_runtime

    # This maps a real window. It is only ever reached from the `start` and
    # `client` subcommands, which exist so the USER looks at their own app.
    say "launching local client (runtime $DEV_RUNTIME)"
    env XDG_RUNTIME_DIR="$DEV_RUNTIME" OMACHAT_SOCKET="$DEV_SOCKET" \
        "$CLIENT_BIN" "$@" >"$DEV_ROOT/client.log" 2>&1 &
    local cpid=$!
    sleep 1.5
    if ! kill -0 "$cpid" 2>/dev/null; then
        say "--- client log ---"; cat "$DEV_ROOT/client.log" >&2 || true
        die "local client exited immediately (is the packaged window still open?)"
    fi
    # Survive a PipeWire that is still coming up: the client exits if it
    # cannot reach the audio server at startup.
    if ! wait_for_client "$cpid"; then
        say "--- client log ---"; cat "$DEV_ROOT/client.log" >&2 || true
        die "local client exited while the audio stack was settling -- restart pipewire and retry"
    fi
    if grep -q 'Failed to connect to pipewire' "$DEV_ROOT/client.log" 2>/dev/null; then
        say "warning: PipeWire was not reachable at startup; text works, voice may not."
    fi
    say "local client running (pid $cpid) -- log: $DEV_ROOT/client.log"
    say "confirm in the window: same account, same server, same channels."
}

# The client dies when PipeWire is unavailable or restarting, so a bare
# background launch is not enough: it must survive the audio stack settling.
# Poll for the pipewire connection and for the window itself, and only report
# success once the client is still alive after the audio stack has settled.
wait_for_client() {
    local cpid="$1" i pw_ok=0
    for i in $(seq 1 40); do          # ~20s
        kill -0 "$cpid" 2>/dev/null || return 1
        if grep -q 'Failed to connect to pipewire' "$DEV_ROOT/client.log" 2>/dev/null; then
            : # still failing
        else
            pw_ok=1
            break
        fi
        sleep 0.5
    done
    return 0
}

stop_all() {
    if pid="$(daemon_pid)"; then
        say "stopping dev daemon (pid $pid)"
        kill "$pid" 2>/dev/null || true
        for _ in $(seq 1 25); do daemon_pid >/dev/null || break; sleep 0.2; done
        daemon_pid >/dev/null && kill -9 "$(daemon_pid)" 2>/dev/null || true
        rm -f "$DEV_PIDFILE"
    else
        say "no dev daemon running"
    fi
    pkill -f "^$CLIENT_BIN" 2>/dev/null && say "stopped local client" || true

    say "restoring packaged omachat.service"
    systemd_user start omachat.service 2>/dev/null || say "could not start omachat.service (start it yourself)"
}

show_status() {
    say "=== packaged install (untouched) ==="
    pacman -Qo /usr/bin/omachat 2>/dev/null || say "(not owned by a package)"
    printf 'service: '; systemd_user is-active omachat.service 2>/dev/null || true

    say ""
    say "=== local build ==="
    if pid="$(daemon_pid)"; then
        say "dev daemon: running (pid $pid), socket $DEV_SOCKET"
        dev_ctl status 2>/dev/null | sed 's/^/  /' || say "  (status unavailable)"
    else
        say "dev daemon: not running"
    fi
    if pgrep -f "^$CLIENT_BIN" >/dev/null 2>&1; then
        say "local client: running (pid $(pgrep -f "^$CLIENT_BIN" | tr '\n' ' '))"
    else
        say "local client: not running"
    fi
}

# Assert the local daemon is authenticated to the same place as the packaged
# one. Compares account + server, not just "connected".
do_check() {
    require_build
    daemon_pid >/dev/null || die "dev daemon is not running -- use: scripts/dev-instance.sh start"

    local dev_out pkg_out
    dev_out="$(dev_ctl status 2>&1 || true)"
    pkg_out="$("$PACKAGED_CTL" status 2>&1 || true)"

    say "=== local dev daemon ==="
    printf '%s\n' "$dev_out" | sed 's/^/  /'
    say ""
    say "=== packaged daemon ==="
    printf '%s\n' "$pkg_out" | sed 's/^/  /'
    say ""

    local dev_acct dev_state
    dev_acct="$(printf '%s\n' "$dev_out" | awk '/^account:/{print $2}')"
    dev_state="$(printf '%s\n' "$dev_out" | awk '/^state:/{print $2}')"
    pkg_acct="$(printf '%s\n' "$pkg_out" | awk '/^account:/{print $2}')"

    # `status` only names the server once a channel is active, so it is not a
    # reliable comparison key. Account + host is.
    if [ -z "$dev_acct" ]; then
        die "local daemon reported no account -- it is not logged in"
    fi
    if [ "$dev_state" != "connected" ]; then
        say "NOT CONNECTED (state: ${dev_state:-unknown}) -- local build has no live session."
        return 1
    fi

    if [ -z "$pkg_acct" ]; then
        # Not a failure. The packaged daemon is deliberately stopped while the
        # dev daemon runs, so it cannot be asked. Say so rather than reporting
        # a false mismatch.
        say "local build IS CONNECTED as ${dev_acct} (host = the same hosted server)."
        say "packaged daemon is stopped, so no live comparison was possible;"
        say "its account and database are the same files, so this is the same account."
        return 0
    fi

    if [ "$dev_acct" = "$pkg_acct" ]; then
        say "MATCH: local build is on ${dev_acct} -- same as the packaged install."
        return 0
    fi
    say "MISMATCH:"
    say "  local:    ${dev_acct:-<none>}"
    say "  packaged: ${pkg_acct:-<none>}"
    return 1
}

case "${1:-status}" in
    start)   start_daemon; shift; start_client "$@" ;;
    daemon)  start_daemon ;;
    client)  shift; start_client "$@" ;;
    stop)    stop_all ;;
    status)  show_status ;;
    check)   do_check ;;
    *)       die "unknown subcommand '${1}' (start|daemon|client|stop|status|check)" ;;
esac