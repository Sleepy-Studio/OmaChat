# shellcheck shell=bash
# Build, run a LOCAL omachat against your real hosted server, and prove it
# landed on the same account -- without replacing /usr/bin.
#
#   ./run-local.sh            build if needed, then start (daemon + GUI)
#   ./run-local.sh --no-build  start only
#   ./run-local.sh stop        back to the packaged install
#   ./run-local.sh status      what's running
#   ./run-local.sh check       assert same account + server as packaged
#
# Everything lives in /tmp/omachat-dev. The packaged install is never modified.

set -euo pipefail

REPO="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$REPO/build"
CMD="${1:-start}"
[ "$CMD" = "stop" ] || [ "$CMD" = "status" ] || [ "$CMD" = "check" ] || [ "$CMD" = "start" ] || {
    printf 'usage: run-local.sh [start|--no-build|stop|status|check]\n' >&2; exit 2; }

if [ "$CMD" = "start" ] && [ "${2:-}" != "--no-build" ]; then
    printf '==> building\n'
    [ -f "$BUILD/build.ninja" ] || cmake -S "$REPO" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build "$BUILD"
fi

exec "$REPO/scripts/dev-instance.sh" "$CMD"