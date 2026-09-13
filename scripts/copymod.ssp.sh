#!/usr/bin/env bash
# copy one built plugin to the SSP, e.g. copymod.ssp.sh attn
set -euo pipefail
MOD="${1:?usage: copymod.ssp.sh <module>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build.cmake.ssp}"
SSP_HOST="${SSP_HOST:-root@192.168.0.150}"

scp -O "$BUILD_DIR"/technobear/*/*/Release/VST3/"$MOD".vst3/Contents/*/"$MOD".so "$SSP_HOST":/media/BOOT/plugins
