#!/usr/bin/env bash
# copy all built plugins to the SSP
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build.cmake.ssp}"
SSP_HOST="${SSP_HOST:-root@192.168.0.150}"

scp -O "$BUILD_DIR"/technobear/*/*/Release/VST3/*.vst3/Contents/*/*.so "$SSP_HOST":/media/BOOT/plugins
