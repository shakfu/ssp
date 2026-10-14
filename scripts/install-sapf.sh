#!/usr/bin/env bash
# copy the UGen plugins and sapf-sc's prelude to the SSP's SD card, mounted locally, as BOOT/sapf/ugens
# and BOOT/sapf/sapf-prelude.txt, where sapf looks for them. sapf has its own copy of the UGens: scsy and
# sapf in one patch must not share the libraries (docs/dev/sapf-plugin.md, Risks).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLUGINS="${SSP_PLUGINS:-/media/$USER/BOOT/plugins}"
DEST="$(dirname "$PLUGINS")/sapf"
UGENS="$ROOT/build/deps/ssp/scsynth/plugins"
SAPF="${SAPF_SC_DIR:-$("$ROOT/scripts/fetch_sapf.sh")}"

[ -d "$(dirname "$PLUGINS")" ] || { echo "$(dirname "$PLUGINS") not found; is the SD card mounted?" >&2; exit 1; }
[ -f "$UGENS/OscUGens.so" ] || { echo "no UGen plugins in $UGENS; run make deps" >&2; exit 1; }

mkdir -p "$DEST"
rm -rf "$DEST/ugens"
cp -r "$UGENS" "$DEST/ugens"
cp -v "$SAPF/sapf-prelude.txt" "$DEST"/
sync  # flush to the card before it is unmounted
