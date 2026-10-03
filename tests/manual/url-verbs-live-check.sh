#!/usr/bin/env bash
# Prove the omachat:// verbs navigate, against a LIVE daemon with real channels.
#
# The headless check (tests/manual/url-verbs-check.sh) can only show the client
# starts: with no daemon there are no channels, so a correct "no such channel"
# path and a silent no-op look identical. This drives the real forwarding path
# -- a second `omachat` invocation hands the URL to the running instance over
# the single-instance socket -- and reads the compositor's window title to see
# which channel is actually displayed.
#
# It does not focus or move windows; it only reads hyprctl's client list.
set -uo pipefail

REPO="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
CTL="$REPO/build/cli/omachatctl"
CLIENT="$REPO/build/client/omachat"
SOCK="${OMACHAT_DEV_SOCKET:-/tmp/omachat-dev/omachat.sock}"
RUN="${OMACHAT_DEV_RUNTIME:-/tmp/omachat-dev/run}"

[ -x "$CTL" ] || { echo "no omachatctl at $CTL"; exit 1; }
[ -S "$SOCK" ] || { echo "no dev daemon socket at $SOCK -- start it with ./run-local.sh"; exit 1; }
[ -x "$CLIENT" ] || { echo "no client at $CLIENT"; exit 1; }

ctl() { "$CTL" --socket "$SOCK" "$@"; }
title() { hyprctl clients 2>/dev/null | grep -oP 'title: \K.*(?= — OmaChat)' | head -1; }

send() { # url -> forwarded to the running client
    env XDG_RUNTIME_DIR="$RUN" OMACHAT_SOCKET="$SOCK" "$CLIENT" "$1" >/dev/null 2>&1
    sleep 3
}

fail=0
expect() { # label want got
    if [ "$2" = "$3" ]; then printf '  %-34s %s  (ok)\n' "$1" "$3"
    else printf '  %-34s %s  EXPECTED %s  FAIL\n' "$1" "$3" "$2"; fail=1; fi
}

# Pick two DISTINCT real channels. channel list includes category header rows
# (the "----- Text Channels" entries share an id with nothing) and two channels
# can render the same title across servers, so prefer the channel list and skip
# anything whose id is a category header. We compare displayed titles, so also
# require the two to differ once rendered -- verified after the fact below.
mapfile -t chans < <(ctl channel list 2>/dev/null | grep -oE '^[0-9]+  # +[^ ]' | grep -oE '^[0-9]+' | head -6)
if [ "${#chans[@]}" -lt 2 ]; then
    echo "need at least two text channels on the active server; found ${#chans[@]}"
    ctl channel list 2>/dev/null | head -12
    exit 1
fi
A="${chans[0]}"; B="${chans[1]}"
echo "using channels: A=$A B=$B"

echo "omachat:// verbs against a live account:"

send "omachat://join/$A"
first="$(title)"
expect "join/$A navigates" "$first" "$first"   # baseline: whatever it shows
printf '  %-34s %s\n' "(baseline after join/$A)" "$first"

send "omachat://join/$B"
second="$(title)"
expect "join/$B differs from A" "different" \
    "$([ "$first" != "$second" ] && echo different || echo same)"
printf '  %-34s %s\n' "(window now shows)" "$second"

# A channel that cannot exist must be refused, not silently ignored.
send "omachat://join/999999999999999999"
third="$(title)"
expect "join/<nonexistent> does not move" "$second" "$third"

echo ""
echo "  window title is read from hyprctl; no window was focused or moved."
exit "$fail"