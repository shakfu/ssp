#!/usr/bin/env bash
# strip built plugins into releases/ssp/plugins and package tb_plugins_ssp.zip
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build.cmake.ssp}"
: "${STRIP:=$(command -v llvm-strip || command -v arm-linux-gnueabihf-strip || true)}"
[ -n "$STRIP" ] || { echo "no llvm-strip or arm-linux-gnueabihf-strip found; set STRIP" >&2; exit 1; }

cd "$ROOT"
cp ./technobear/README.txt ./releases
cp "$BUILD_DIR"/technobear/*/*/Release/VST3/*.vst3/Contents/*/*.so ./releases/ssp/plugins
"$STRIP" --strip-unneeded ./releases/ssp/plugins/*

rm -rf tmp
mkdir -p tmp
cd tmp

cp ../releases/README.txt .
cp -r ../releases/other .
cp -r ../releases/ssp/plugins plugins

cp ../resources/ssp/* .
if [ -f SYNTHOR.zip ]; then
    unzip SYNTHOR.zip
    rm SYNTHOR.zip
fi
rm -f ../tb_plugins_ssp.zip
zip -r ../tb_plugins_ssp.zip .

cd ..
rm -rf tmp
