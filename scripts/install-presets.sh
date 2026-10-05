#!/usr/bin/env bash
# copy rack presets to the SSP's SD card, mounted locally: install-presets.sh [dir]
# JSON presets are checked with py2rack first; any error stops the copy.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${1:-$ROOT/presets}"
DEST="${SSP_PRESETS:-/media/$USER/rootfs/rack_presets}"
PY2RACK="$ROOT/tools/py2rack/py2rack.py"
MANIFEST="$ROOT/tools/py2rack/modules.json"

[ -d "$SRC" ] || { echo "$SRC not found; put presets there or pass a directory" >&2; exit 1; }
[ -d "$(dirname "$DEST")" ] || { echo "$(dirname "$DEST") not found; is the SD card mounted?" >&2; exit 1; }

shopt -s nullglob
files=()
for f in "$SRC"/*; do [ -f "$f" ] && files+=("$f"); done
[ ${#files[@]} -gt 0 ] || { echo "no presets in $SRC" >&2; exit 1; }

for f in "$SRC"/*.json; do
    python3 "$PY2RACK" encode "$f" -o /dev/null -m "$MANIFEST" || { echo "in $f" >&2; exit 1; }
done

# rootfs is owned by root, so sudo unless the destination (or its parent, to create it) is writable
target="$DEST"
[ -e "$DEST" ] || target="$(dirname "$DEST")"
SUDO=""
[ -w "$target" ] || SUDO=sudo
$SUDO mkdir -p "$DEST"
$SUDO cp -v "${files[@]}" "$DEST"/
sync  # flush to the card before it is unmounted
