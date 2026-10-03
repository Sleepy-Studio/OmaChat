#!/usr/bin/env bash
# Headless check for the omachat:// verbs in AppController::openLink.
#
# WHAT THIS PROVES, PRECISELY: the client starts and renders with each URL, so
# no verb crashes the parser, the engine, or startup. It does NOT prove a verb
# navigated anywhere -- without a live daemon there are no channels to select,
# so a correct "no such channel" path and a silent no-op look identical from the
# outside. Confirming a verb actually navigated needs a running daemon and a
# human eye. See docs/addons.md Phase 2.
#
# Uses the same path as the CI GUI smoke test (--screenshot + offscreen), so it
# never maps a window. Point it at a scratch XDG_RUNTIME_DIR: the client takes a
# single-instance lock from runtimeDir(), and the real one may be in use.
set -uo pipefail

REPO="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN="$REPO/build/client/omachat"
RT="${OMACHAT_TEST_RUNTIME:-/tmp/omachat-urltest}"
SHOT="/tmp/omachat-urltest.png"

[ -x "$BIN" ] || { echo "no client build at $BIN"; exit 1; }

mkdir -p -m 700 "$RT"
# Run with a scrubbed environment. This shell may already export a platform or
# a display variable that Qt honours ahead of QT_QPA_PLATFORM, which silently
# sends the client to GTK/X11 instead of offscreen.
run_case() {
    local url="$1"
    rm -f "$SHOT"
    env -i PATH="/usr/local/bin:/usr/bin:/bin" HOME="$HOME" \
        XDG_RUNTIME_DIR="$RT" \
        OMACHAT_SOCKET="/tmp/omachat-urltest-none.sock" \
        QT_QPA_PLATFORM=offscreen \
        QT_QUICK_BACKEND=software \
        "$BIN" --screenshot "$SHOT" "$url" >/tmp/omachat-urltest.log 2>&1
    local rc=$?
    if [ -s "$SHOT" ]; then
        printf '  %-34s rendered (exit %d)\n' "$url" "$rc"
        return 0
    fi
    printf '  %-34s FAILED (exit %d)\n' "$url" "$rc"
    sed 's/^/      /' /tmp/omachat-urltest.log | tail -5
    return 1
}

fail=0
echo "omachat:// verbs (startup and render only -- see header for limits):"
for u in "omachat://join/999" "omachat://join/" "omachat://server/123" \
         "omachat://server/" "omachat://bogus/x" "omachat://"; do
    run_case "$u" || fail=1
done
rm -f "$SHOT"
exit "$fail"