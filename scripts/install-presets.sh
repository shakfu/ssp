#!/usr/bin/env bash
# copy rack presets to the SSP's SD card (ssp-card.sh: mounted, or over ssh) as BOOT/rack_presets:
# install-presets.sh [dir]. SSP_PRESETS names another local destination, with sudo where it is not
# writable (upstream trax reads trax_presets on rootfs). JSON presets are checked with py2rack first; any
# error stops the copy.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${1:-$ROOT/presets}"
PY2RACK="$ROOT/tools/py2rack/py2rack.py"
# TheTechnobear's modules, scanned from source; then this repo's, read from the built plugins
MANIFESTS=(-m "$ROOT/tools/py2rack/modules.json" -m "$ROOT/tools/py2rack/modules-local.json")

[ -d "$SRC" ] || { echo "$SRC not found; put presets there or pass a directory" >&2; exit 1; }

shopt -s nullglob
files=()
for f in "$SRC"/*; do [ -f "$f" ] && files+=("$f"); done
[ ${#files[@]} -gt 0 ] || { echo "no presets in $SRC" >&2; exit 1; }

for f in "$SRC"/*.json; do
    python3 "$PY2RACK" encode "$f" -o /dev/null "${MANIFESTS[@]}" || { echo "in $f" >&2; exit 1; }
done

if [ -z "${SSP_PRESETS:-}" ]; then
    source "$ROOT/scripts/ssp-card.sh"
    card_files rack_presets "${files[@]}"
    card_sync
    exit 0
fi

# sudo only for a destination (or its parent, to create it) that is not writable, such as one on rootfs
DEST="$SSP_PRESETS"
[ -d "$(dirname "$DEST")" ] || { echo "$(dirname "$DEST") not found; is the SD card mounted?" >&2; exit 1; }
target="$DEST"
[ -e "$DEST" ] || target="$(dirname "$DEST")"
SUDO=""
[ -w "$target" ] || SUDO=sudo
$SUDO mkdir -p "$DEST"
$SUDO cp -v "${files[@]}" "$DEST"/
sync  # flush to the card before it is unmounted
