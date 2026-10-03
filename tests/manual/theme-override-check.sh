#!/usr/bin/env bash
# Headless check for the OmaChat-local theme override.
#
# Proves the local theme file wins over the Omarchy one: it renders the client
# offscreen with each of three setups and compares the background pixel of the
# resulting screenshot.
#
#   omarchy   only $XDG_STATE_HOME/.../colors.toml  -> that colour
#   local     both files, different colours        -> the local one
#   partial   local file missing background         -> falls back to Omarchy
#   none      no theme files at all                 -> built-in fallback, renders
#
# Never maps a window: offscreen platform, software backend, scrubbed env.
set -uo pipefail

REPO="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN="$REPO/build/client/omachat"
WORK="$(mktemp -d /tmp/omachat-theme-XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

[ -x "$BIN" ] || { echo "no client build at $BIN"; exit 1; }

RT="$WORK/run"; mkdir -p -m 700 "$RT"
THEMEDIR="$WORK/state/omarchy/current/theme"; mkdir -p "$THEMEDIR"

# Deliberately loud, unmistakable colours. Magenta can never be confused with
# the built-in dark palette.
cat > "$THEMEDIR/colors.toml" <<'EOF'
background = "#00ff00"
foreground = "#ffffff"
accent = "#00ff00"
EOF

cat > "$WORK/local-full.toml" <<'EOF'
background = "#ff00ff"
foreground = "#ffffff"
EOF

# Only accent: background is absent, so the Omarchy background must win.
cat > "$WORK/local-partial.toml" <<'EOF'
accent = "#ffff00"
EOF

render() { # $1=out png, rest=env assignments handled by caller via globals
    rm -f "$1"
    env -i PATH="/usr/local/bin:/usr/bin:/bin" HOME="$HOME" \
        XDG_RUNTIME_DIR="$RT" \
        XDG_STATE_HOME="$WORK/state" \
        XDG_CONFIG_HOME="$WORK/config" \
        OMACHAT_SOCKET="$WORK/none.sock" \
        QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
        $EXTRA_ENV "$BIN" --screenshot "$1" >"$WORK/log" 2>&1
}

# Average RGB of the whole image; OmaChat's window is a single flat-ish surface
# so the mean is dominated by the background.
bg_of() {
    python3 - "$1" <<'PY'
import struct, sys, zlib
data = open(sys.argv[1], 'rb').read()
pos, idat, w, h, bd, ct = 8, b'', 0, 0, 0, 0
while pos < len(data):
    ln = struct.unpack('>I', data[pos:pos+4])[0]
    typ = data[pos+4:pos+8]
    body = data[pos+8:pos+8+ln]
    if typ == b'IHDR':
        w, h, bd, ct = struct.unpack('>IIBB', body[:10])
    elif typ == b'IDAT':
        idat += body
    pos += 12 + ln
raw = zlib.decompress(idat)
ch = {0:1, 2:3, 3:1, 4:2, 6:4}[ct]
stride = w * ch
prev = bytearray(stride)
rows, i = [], 0
for _ in range(h):
    f = raw[i]; i += 1
    line = bytearray(raw[i:i+stride]); i += stride
    for x in range(stride):
        a = line[x-ch] if x >= ch else 0
        b = prev[x]
        c = prev[x-ch] if x >= ch else 0
        if f == 1:   line[x] = (line[x]+a) & 255
        elif f == 2: line[x] = (line[x]+b) & 255
        elif f == 3: line[x] = (line[x]+(a+b)//2) & 255
        elif f == 4:
            p = a+b-c
            pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
            pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
            line[x] = (line[x]+pr) & 255
    prev = line
    rows.append(bytes(line))
step = max(1, h // 40)
rs = gs = bs = n = 0
for r in range(0, h, step):
    row = rows[r]
    for x in range(0, w, step * 4):
        o = x * ch
        rs += row[o]; gs += row[o+1] if ch > 1 else row[o]; bs += row[o+2] if ch > 2 else row[o]; n += 1
print(f"{rs//n:02x}{gs//n:02x}{bs//n:02x}")
PY
}

fail=0
# ThemeProvider::derive mixes and darkens the declared colors, so the rendered
# pixel is close to -- but not exactly -- the hex in the file. Compare per
# channel with a tolerance, and still require the expected hue to dominate.
check() { # label expected actual
    python3 - "$1" "$2" "$3" <<'PY' || fail=1
import sys
label, exp, act = sys.argv[1], sys.argv[2], sys.argv[3]
tol = 12
try:
    e = [int(exp[i:i+2], 16) for i in (0, 2, 4)]
    a = [int(act[i:i+2], 16) for i in (0, 2, 4)]
except ValueError:
    print(f"  {label:<22} {act}  MALFORMED  FAIL"); sys.exit(1)
if all(abs(x - y) <= tol for x, y in zip(e, a)):
    print(f"  {label:<22} {act} (ok, expected ~{exp})"); sys.exit(0)
print(f"  {label:<22} {act}  EXPECTED ~{exp}  FAIL"); sys.exit(1)
PY
}

echo "local theme override:"

# 1. Omarchy only -> green background.
EXTRA_ENV=""
render "$WORK/omarchy.png"
[ -s "$WORK/omarchy.png" ] || { echo "  omarchy-only        RENDER FAILED"; sed 's/^/    /' "$WORK/log" | tail -5; exit 1; }
check "omarchy-only" "00ff00" "$(bg_of "$WORK/omarchy.png")"

# 2. Local present -> local magenta must win over Omarchy green.
EXTRA_ENV="OMACHAT_THEME_FILE=$WORK/local-full.toml"
render "$WORK/local.png"
[ -s "$WORK/local.png" ] || { echo "  local-wins          RENDER FAILED"; exit 1; }
check "local-wins" "ff00ff" "$(bg_of "$WORK/local.png")"

# 3. Partial local (no background) -> Omarchy green must win, not the built-in.
EXTRA_ENV="OMACHAT_THEME_FILE=$WORK/local-partial.toml"
render "$WORK/partial.png"
[ -s "$WORK/partial.png" ] || { echo "  partial-fallback    RENDER FAILED"; exit 1; }
check "partial-fallback" "00ff00" "$(bg_of "$WORK/partial.png")"

# 4. No theme at all -> built-in dark, and it still renders.
rm -f "$THEMEDIR/colors.toml"
EXTRA_ENV=""
render "$WORK/builtin.png"
[ -s "$WORK/builtin.png" ] && printf '  %-22s rendered (ok)\n' "builtin-fallback" || { echo "  builtin-fallback    RENDER FAILED"; fail=1; }

exit "$fail"