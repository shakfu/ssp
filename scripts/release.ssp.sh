#!/usr/bin/env bash
# strip built plugins into releases/ssp/plugins and package ssp_plugins.zip
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build.cmake.ssp}"
: "${STRIP:=$(command -v llvm-strip || command -v arm-linux-gnueabihf-strip || true)}"
[ -n "$STRIP" ] || { echo "no llvm-strip or arm-linux-gnueabihf-strip found; set STRIP" >&2; exit 1; }

cd "$ROOT"
mkdir -p releases/ssp/plugins
cp "$BUILD_DIR"/plugins/*/*/Release/VST3/*.vst3/Contents/*/*.so ./releases/ssp/plugins
"$STRIP" --strip-unneeded ./releases/ssp/plugins/*

rm -rf tmp
mkdir -p tmp
cd tmp

cp ../LICENSE .
cp -r ../releases/ssp/plugins plugins

cp ../resources/ssp/* .
if [ -f SYNTHOR.zip ]; then
    unzip SYNTHOR.zip
    rm SYNTHOR.zip
fi
rm -f ../ssp_plugins.zip
zip -r ../ssp_plugins.zip .

cd ..
rm -rf tmp
