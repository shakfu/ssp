#!/usr/bin/env bash
# build one plugin and copy it to the SSP over ssh, e.g. copymod.ssp.sh sapf
# Builds only that plugin's target. Refuses while Synthor runs: replacing a loaded plugin's .so can crash
# it. Syncs the card and checks the copy.
set -euo pipefail
MOD="${1:?usage: copymod.ssp.sh <module>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build.cmake.ssp}"
SSP_HOST="${SSP_HOST:-root@192.168.1.6}"
JOBS="${JOBS:-$(nproc)}"

# the CMake project whose PRODUCT_NAME is MOD; its VST3 target is <PROJECT>_VST3
cmake_file=$(grep -l "PRODUCT_NAME \"$MOD\"" "$ROOT"/plugins/*/CMakeLists.txt || true)
[ -n "$cmake_file" ] || { echo "no plugin named $MOD in plugins/*/CMakeLists.txt" >&2; exit 1; }
project=$(sed -n 's/^project(\([A-Za-z0-9_]*\).*/\1/p' "$cmake_file" | head -1)
cmake --build "$BUILD_DIR" --target "${project}_VST3" -j"$JOBS"

so=$(ls "$BUILD_DIR"/plugins/*/"${project}"_artefacts/Release/VST3/"$MOD".vst3/Contents/*/"$MOD".so)
if ssh -o BatchMode=yes "$SSP_HOST" 'pidof SYNTHOR >/dev/null'; then
    echo "Synthor is running on $SSP_HOST: stop it first; replacing a loaded plugin can crash it" >&2
    exit 1
fi
scp -O -q -o BatchMode=yes "$so" "$SSP_HOST":/media/BOOT/plugins/
remote=$(ssh -o BatchMode=yes "$SSP_HOST" "sync && md5sum /media/BOOT/plugins/$MOD.so" | cut -d' ' -f1)
local=$(md5sum "$so" | cut -d' ' -f1)
[ "$remote" = "$local" ] || { echo "$MOD.so on $SSP_HOST ($remote) differs from the build ($local)" >&2; exit 1; }
echo "$MOD.so -> $SSP_HOST:/media/BOOT/plugins ($local)"
