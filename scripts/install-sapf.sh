#!/usr/bin/env bash
# copy sapf's prelude and its examples to the SSP's SD card, mounted locally, as BOOT/sapf/sapf-prelude.txt
# and BOOT/sapf/sapf-examples.txt, where the sapf plugin looks for them.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLUGINS="${SSP_PLUGINS:-/media/$USER/BOOT/plugins}"
DEST="$(dirname "$PLUGINS")/sapf"
SAPF="${SAPF_DIR:-$("$ROOT/scripts/fetch_sapf.sh")}"

[ -d "$(dirname "$PLUGINS")" ] || { echo "$(dirname "$PLUGINS") not found; is the SD card mounted?" >&2; exit 1; }

mkdir -p "$DEST"
cp -v "$SAPF/sapf-prelude.txt" "$SAPF/sapf-examples.txt" "$DEST"/
sync  # flush to the card before it is unmounted
