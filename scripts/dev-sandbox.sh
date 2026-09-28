#!/usr/bin/env bash
# Throwaway local OmaChat for manual testing, run from ./build:
# a server on 127.0.0.1:26473, a "bob" helper daemon (no audio) and your
# own daemon with real PipeWire, plus the GUI. Nothing touches your real
# config, cache or keyring: every path lives under $OMACHAT_SANDBOX and
# logins are kept in memory.
#
#   scripts/dev-sandbox.sh start    build dir must exist; opens the GUI
#   scripts/dev-sandbox.sh bob ARGS omachatctl as bob, e.g. message send general hi
#   scripts/dev-sandbox.sh stop     kill everything and delete the sandbox
#
# Accounts: howie / bob, password "testpass123".
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=${OMACHAT_BUILD:-$ROOT/build}
S=${OMACHAT_SANDBOX:-${XDG_RUNTIME_DIR:-/tmp}/omachat-sandbox}
R=${XDG_RUNTIME_DIR:-/tmp}   # sockets stay short: sun_path is 107 bytes
PORT=26473
PASS=testpass123
CTL="$B/cli/omachatctl"

pids() { cat "$S"/*.pid 2>/dev/null || true; }

spawn() { # name, command...
    local name=$1
    shift
    setsid "$@" >"$S/$name.log" 2>&1 < /dev/null &
    echo $! >"$S/$name.pid"
}

register() { # who, username
    local sock=$R/omachat-sandbox-$1.sock fp
    for _ in 1 2 3 4 5 6 7 8 9 10; do [[ -S $sock ]] && break; sleep 0.2; done
    echo "$PASS" | "$CTL" --socket "$sock" account register "localhost:$PORT" "$2" --password-stdin >/dev/null 2>&1 || true
    sleep 0.5
    fp=$("$CTL" --socket "$sock" status --json |
        python3 -c 'import sys,json;print((json.load(sys.stdin).get("error") or {}).get("fingerprint",""))')
    if [[ -n $fp ]]; then
        "$CTL" --socket "$sock" trust "$fp" >/dev/null
        sleep 0.7
        echo "$PASS" | "$CTL" --socket "$sock" account register "localhost:$PORT" "$2" --password-stdin >/dev/null
    fi
    sleep 0.8
    "$CTL" --socket "$sock" status | head -1
}

start() {
    [[ -x $B/server/omachat-server ]] || { echo "build first: cmake --build $B" >&2; exit 1; }
    [[ -d $S ]] && { echo "sandbox already exists at $S (run stop first)" >&2; exit 1; }
    mkdir -p "$S"/{srv,you,bob,files}
    "$B/server/omachat-server" generate-cert --cert "$S/srv/cert.pem" --key "$S/srv/key.pem" --name localhost >/dev/null
    cat >"$S/srv/server.toml" <<EOF
[server]
name = "OmaChat Sandbox"
bind = "127.0.0.1"
port = $PORT
[media]
udp_port = $((PORT + 1))
[database]
path = "$S/srv/db.sqlite"
[tls]
certificate = "$S/srv/cert.pem"
private_key = "$S/srv/key.pem"
[files]
path = "$S/srv/files"
max_upload_mb = 50
EOF
    spawn server "$B/server/omachat-server" -c "$S/srv/server.toml"
    for who in bob you; do
        local extra=()
        [[ $who == bob ]] && extra=(--null-audio --no-notifications)
        spawn "daemon-$who" env XDG_CONFIG_HOME="$S/$who/cfg" XDG_CACHE_HOME="$S/$who/cache" \
            "$B/daemon/omachatd" --socket "$R/omachat-sandbox-$who.sock" --database "$S/$who/db" \
            --config "$S/$who/config.toml" --memory-credentials "${extra[@]}"
    done
    sleep 1
    register bob bob
    register you howie

    local bob=("$CTL" --socket "$R/omachat-sandbox-bob.sock") invite
    "${bob[@]}" server create "Sleepy Studio" >/dev/null
    invite=$("${bob[@]}" --json invite create "Sleepy Studio" | python3 -c 'import sys,json;print(json.load(sys.stdin)["token"])')
    "$CTL" --socket "$R/omachat-sandbox-you.sock" server join "$invite" >/dev/null
    cp "$ROOT/docs/screenshots/main.png" "$S/files/omachat-layout.png"
    printf 'Release checklist\n- attachments\n- screen sharing next\n' >"$S/files/notes.txt"
    "${bob[@]}" message send general "hey, attachments landed — try dragging a file in" >/dev/null
    "${bob[@]}" message send general "old screenshot for comparison" --attach "$S/files/omachat-layout.png" >/dev/null
    "${bob[@]}" message send general --attach "$S/files/notes.txt" >/dev/null

    spawn gui env OMACHAT_SOCKET="$R/omachat-sandbox-you.sock" XDG_CONFIG_HOME="$S/you/cfg" \
        XDG_CACHE_HOME="$S/you/cache" "$B/client/omachat"
    echo "sandbox up at $S — GUI opened as howie ($PASS); logs in $S/*.log"
}

stop() {
    local p
    for p in $(pids); do kill "$p" 2>/dev/null || true; done
    sleep 0.5
    rm -f "$R"/omachat-sandbox-{you,bob}.sock
    rm -rf "$S"
    echo "sandbox stopped"
}

case ${1:-} in
start) start ;;
stop) stop ;;
bob) shift; exec "$CTL" --socket "$R/omachat-sandbox-bob.sock" "$@" ;;
*) sed -n '2,12p' "$0"; exit 2 ;;
esac
