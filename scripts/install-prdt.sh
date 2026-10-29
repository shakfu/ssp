#!/usr/bin/env bash
# copy prdt's example patches and the Pd extra/, cyclone and ELSE abstractions to the SSP's SD card (ssp-card.sh:
# mounted, or over ssh) as BOOT/prdt/*.pd, BOOT/prdt/extra/*.pd, BOOT/prdt/cyclone/*.pd and BOOT/prdt/else/*.pd,
# ELSE's folders flat. Synthor may run: patches are read only on a load.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT/scripts/ssp-card.sh"

card_files prdt "$ROOT"/plugins/prdt/examples/*.pd
card_files prdt/extra "$ROOT"/external/libpd/pure-data/extra/*.pd
card_files prdt/cyclone "$ROOT"/external/pd-cyclone/cyclone_objects/abstractions/*.pd
card_files prdt/else "$ROOT"/external/pd-else/Abstractions/{Audio,Control,Extra,Merda/Modules}/*.pd
card_sync
