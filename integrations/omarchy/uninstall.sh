#!/bin/bash
# Disables and removes the OmaChat bar widget. OmaChat itself is unaffected.
set -euo pipefail
ID=io.github.howieduhzit.omachat
DEST="${XDG_CONFIG_HOME:-$HOME/.config}/omarchy/plugins/$ID"
omarchy plugin disable "$ID" >/dev/null 2>&1 || true
rm -rf -- "$DEST"
omarchy-shell shell rescanPlugins >/dev/null 2>&1 || true
echo "Removed $ID"
