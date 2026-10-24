#!/usr/bin/env bash
# copy the Faust libraries and the fstr examples to the SSP's SD card (ssp-card.sh: mounted, or over ssh)
# as BOOT/faust/libraries and BOOT/faust/*.dsp, where fstr looks for them
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LIBS="$ROOT/build/deps/ssp/share/faust"

[ -f "$LIBS/stdfaust.lib" ] || { echo "no Faust libraries in $LIBS; run make deps" >&2; exit 1; }
source "$ROOT/scripts/ssp-card.sh"

card_tree "$LIBS" faust/libraries
card_files faust "$ROOT"/plugins/faust/examples/*.dsp
card_sync
