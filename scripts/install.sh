#!/usr/bin/env bash
# copy built plugins to the SSP's SD card, mounted locally: install.sh [module ...]
# with no modules, copies all of them
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build.cmake.ssp}"
DEST="${SSP_PLUGINS:-/media/$USER/BOOT/plugins}"

[ -d "$DEST" ] || { echo "$DEST not found; is the SD card mounted?" >&2; exit 1; }

shopt -s nullglob
if [ $# -eq 0 ]; then
    files=("$BUILD_DIR"/plugins/*/*/Release/VST3/*.vst3/Contents/*/*.so)
    [ ${#files[@]} -gt 0 ] || { echo "no plugins in $BUILD_DIR; run make first" >&2; exit 1; }
else
    files=()
    for mod in "$@"; do
        found=("$BUILD_DIR"/plugins/*/*/Release/VST3/"$mod".vst3/Contents/*/"$mod".so)
        [ ${#found[@]} -gt 0 ] || { echo "$mod is not built in $BUILD_DIR" >&2; exit 1; }
        files+=("${found[@]}")
    done
fi

cp -v "${files[@]}" "$DEST"/
sync  # flush to the card before it is unmounted
