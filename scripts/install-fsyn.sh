#!/usr/bin/env bash
# copy FluidR3_GM.sf2, the soundfont scripts/build_fluidsynth.sh fetches, to the SSP's SD card (ssp-card.sh:
# mounted, or over ssh) as BOOT/fsyn/FluidR3_GM.sf2, which a new fsyn loads.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FONT="$ROOT/build/deps/src/FluidR3_GM.sf2"

[ -f "$FONT" ] || { echo "no $FONT: run make deps" >&2; exit 1; }
source "$ROOT/scripts/ssp-card.sh"

card_files fsyn "$FONT"
card_sync
