#!/usr/bin/env bash
# copy the UGen plugins and the scsy examples to the SSP's SD card, mounted locally, as
# BOOT/scsy/ugens and BOOT/scsy/*.scsyndef, where scsy looks for them
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLUGINS="${SSP_PLUGINS:-/media/$USER/BOOT/plugins}"
DEST="$(dirname "$PLUGINS")/scsy"
UGENS="$ROOT/build/deps/ssp/scsynth/plugins"

[ -d "$(dirname "$PLUGINS")" ] || { echo "$(dirname "$PLUGINS") not found; is the SD card mounted?" >&2; exit 1; }
[ -f "$UGENS/OscUGens.so" ] || { echo "no UGen plugins in $UGENS; run make deps" >&2; exit 1; }

mkdir -p "$DEST"
rm -rf "$DEST/ugens"
cp -r "$UGENS" "$DEST/ugens"
cp -v "$ROOT"/plugins/scsynth/examples/* "$DEST"/
sync  # flush to the card before it is unmounted
