#!/bin/bash
# Installs the OmaChat Omarchy bar widget into the user's plugin directory
# (copy, not symlink: Omarchy rejects symlinked plugins), then rescans and
# enables it. Reversible with uninstall.sh. Never touches the native app.
set -euo pipefail
ID=io.github.howieduhzit.omachat
SRC="$(cd "$(dirname "$0")" && pwd)/$ID"
DEST="${XDG_CONFIG_HOME:-$HOME/.config}/omarchy/plugins/$ID"
command -v omarchy >/dev/null || { echo "omarchy not found; the native app works without this plugin." >&2; exit 1; }
omarchy plugin validate "$SRC"
mkdir -p "$DEST"
cp -f "$SRC"/manifest.json "$SRC"/*.qml "$SRC"/README.md "$DEST"/
omarchy-shell shell rescanPlugins >/dev/null 2>&1 || true
omarchy plugin enable "$ID"
echo "Installed $ID to $DEST"
