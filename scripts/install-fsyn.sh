#!/usr/bin/env bash
# copy FluidR3_GM.sf2, the soundfont scripts/build_fluidsynth.sh fetches, to the SSP's SD card, mounted
# locally, as BOOT/fsyn/FluidR3_GM.sf2, which a new fsyn loads.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLUGINS="${SSP_PLUGINS:-/media/$USER/BOOT/plugins}"
DEST="$(dirname "$PLUGINS")/fsyn"
FONT="$ROOT/build/deps/src/FluidR3_GM.sf2"

[ -f "$FONT" ] || { echo "no $FONT: run make deps" >&2; exit 1; }
[ -d "$(dirname "$PLUGINS")" ] || { echo "$(dirname "$PLUGINS") not found; is the SD card mounted?" >&2; exit 1; }

mkdir -p "$DEST"
cp -v "$FONT" "$DEST"/
sync  # flush to the card before it is unmounted
