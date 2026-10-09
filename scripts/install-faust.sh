#!/usr/bin/env bash
# copy the Faust libraries and the fstr examples to the SSP's SD card, mounted locally, as
# BOOT/faust/libraries and BOOT/faust/*.dsp, where fstr looks for them
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLUGINS="${SSP_PLUGINS:-/media/$USER/BOOT/plugins}"
DEST="$(dirname "$PLUGINS")/faust"
LIBS="$ROOT/build/deps/ssp/share/faust"

[ -d "$(dirname "$PLUGINS")" ] || { echo "$(dirname "$PLUGINS") not found; is the SD card mounted?" >&2; exit 1; }
[ -f "$LIBS/stdfaust.lib" ] || { echo "no Faust libraries in $LIBS; run make deps" >&2; exit 1; }

mkdir -p "$DEST"
rm -rf "$DEST/libraries"
cp -r "$LIBS" "$DEST/libraries"
cp -v "$ROOT"/plugins/faust/examples/*.dsp "$DEST"/
sync  # flush to the card before it is unmounted
