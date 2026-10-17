#!/usr/bin/env bash
# Re-authenticate an omachat account against a server, then report what happened.
#
# WHY THIS EXISTS
# ---------------
# A stored account can stop authenticating while the server itself is healthy:
# TCP open, TLS 1.3 negotiating, then "The remote host closed the connection"
# during the handshake. That is an application-layer refusal, almost always a
# session token the server no longer accepts (a redeploy, a key rotation, or a
# database reset). Re-running `account login` with the password mints a fresh
# token; `account login` reuses the existing account row when host/port/username
# match (DaemonMethods.cpp ensureAccount), so nothing else is lost.
#
# It will NOT fix a wrong password, a suspended account, or a server that is
# refusing everyone -- those need the server operator, not a retry.
#
# Usage:
#   scripts/reauth.sh                       # re-auth every stored account, no prompt
#   scripts/reauth.sh HOST:PORT USERNAME    # one account
#   OMACHAT_PASSWORD=... scripts/reauth.sh  # non-interactive; prefer this in CI
#
# Reads OMACHAT_SOCKET, so it works against either the packaged daemon or a dev
# instance. Never prints the password or the token.
set -uo pipefail

REPO="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
CTL="${OMACHAT_CTL:-$REPO/build/cli/omachatctl}"
[ -x "$CTL" ] || CTL=/usr/bin/omachatctl
[ -x "$CTL" ] || { echo "reauth: no omachatctl found" >&2; exit 1; }

CTL_ARGS=()
[ -n "${OMACHAT_SOCKET:-}" ] && CTL_ARGS=(--socket "$OMACHAT_SOCKET")

say() { printf '%s\n' "$*"; }

# Never echo the password; use read -s so it is not on the terminal scrollback,
# and allow OMACHAT_PASSWORD for non-interactive use.
ask_password() {
    if [ -n "${OMACHAT_PASSWORD:-}" ]; then printf '%s' "$OMACHAT_PASSWORD"; return; fi
    if [ ! -t 0 ]; then echo "reauth: no TTY and OMACHAT_PASSWORD unset" >&2; return 1; fi
    printf 'password: ' >&2
    local p; IFS= read -rs p </dev/tty || p=""
    printf '\n' >&2
    printf '%s' "$p"
}

status_line() { "$CTL" "${CTL_ARGS[@]}" status 2>&1 | grep -E '^(state|account|error):' || true; }

reauth_one() { # host_port username
    local target=$1 user=$2 pw rc
    say "--- re-authenticating $user@$target"
    if ! pw="$(ask_password)" || [ -z "$pw" ]; then
        say "    skipped: no password"
        return 1
    fi
    printf '%s' "$pw" | "$CTL" "${CTL_ARGS[@]}" account login "$target" "$user" --password-stdin >/tmp/reauth.out 2>&1
    rc=$?
    if [ $rc -eq 0 ]; then
        say "    login accepted"
    else
        say "    login FAILED:"
        sed 's/^/      /' /tmp/reauth.out
    fi
    rm -f /tmp/reauth.out
    # Give the daemon a moment to finish the handshake before we judge it.
    local i
    for i in 1 2 3 4 5 6 7 8 9 10; do
        "$CTL" "${CTL_ARGS[@]}" status 2>/dev/null | grep -q 'state:     connected' && break
        sleep 1
    done
    status_line | sed 's/^/    /'
    return $rc
}

# Wait for the server to be reachable at all, so a network problem is reported
# as such instead of as a credential problem.
probe_server() { # host port
    timeout 8 bash -c "exec 3<>/dev/tcp/$1/$2" 2>/dev/null
}

if [ $# -ge 2 ]; then
    reauth_one "$1" "$2"
    exit $?
fi

say "=== accounts ==="
accounts="$("$CTL" "${CTL_ARGS[@]}" account list 2>/dev/null)"
printf '%s\n' "$accounts" | sed 's/^/  /'
if [ -z "$accounts" ]; then
    say "no accounts stored; log in with: omachatctl account login HOST:PORT USERNAME"
    exit 1
fi

host=$(printf '%s\n' "$accounts" | awk '/omka@/{print "omachat.sleepystudio.xyz:6473"; exit}')
for line in $(printf '%s\n' "$accounts" | grep -oE '[A-Za-z0-9._-]+@[A-Za-z0-9.-]+:[0-9]+'); do
    user=${line%@*}; hostport=${line#*@}
    say ""
    if ! probe_server "${hostport%:*}" "${hostport##*:}"; then
        say "--- $user@$hostport: server not reachable, skipping"
        continue
    fi
    reauth_one "$hostport" "$user"
done

say ""
say "=== final status ==="
status_line | sed 's/^/  /'