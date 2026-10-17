# shellcheck shell=bash
# Fetch every push to origin/main, build, and launch the current local omachat
# against your real hosted server -- without replacing /usr/bin.
#
#   ./run-local.sh            fetch, fast-forward, build, and start
#   ./run-local.sh stop        back to the packaged install
#   ./run-local.sh status      what's running
#   ./run-local.sh check       assert same account + server as packaged
#   OMACHAT_AUTO_UPDATE=0 ./run-local.sh   build this checkout once, without fetching
#
# The omachat theme in ~/.config/omachat/theme.toml is kept across updates.

set -euo pipefail

REPO="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CMD="${1:-start}"
[ "$CMD" = "stop" ] || [ "$CMD" = "status" ] || [ "$CMD" = "check" ] || [ "$CMD" = "start" ] || {
    printf 'usage: run-local.sh [start|stop|status|check]\n' >&2; exit 2; }

exec "$REPO/scripts/dev-instance.sh" "$CMD"
