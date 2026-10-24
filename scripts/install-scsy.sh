#!/usr/bin/env bash
# copy the UGen plugins and the scsy examples to the SSP's SD card (ssp-card.sh: mounted, or over ssh) as
# BOOT/scsy/ugens and BOOT/scsy/*.scsyndef, where scsy looks for them
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UGENS="$ROOT/build/deps/ssp/scsynth/plugins"

[ -f "$UGENS/OscUGens.so" ] || { echo "no UGen plugins in $UGENS; run make deps" >&2; exit 1; }
source "$ROOT/scripts/ssp-card.sh"

card_tree "$UGENS" scsy/ugens
card_files scsy "$ROOT"/plugins/scsynth/examples/*
card_sync
