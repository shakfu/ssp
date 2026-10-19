#!/usr/bin/env bash
# Fetch schollz/_core, the EZEPTOCORE firmware the ezep plugin runs, into build/deps/src/core-REF, and
# generate its desktop engine for TARGET (host or ssp) into core-REF/gen/TARGET/engine.c. Only lib/ and
# main.c, which _core's generator reads, rack/src (its card reader) and the licence are kept.
#
# The generator, _core's lib/core_engine/prepare.py, needs clang (its JSON AST) and numpy, run here
# through uv. It moves the firmware's globals into a per-instance struct; the result includes the
# target's preprocessed libc, so each target gets its own. plugins/ezeptocore/Source/ssp_api.inc joins
# the firmware source between prepare.py's two stages, so its functions are rewritten with the rest.
#
# GitHub's archive tarballs are checked against SHA-256. GitHub has changed their compression before;
# a mismatch after such a change needs the hash updated, not the check removed.
#
# usage: scripts/fetch_core.sh host|ssp   (prints the directory)

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src="$root/build/deps/src"
target="${1:?usage: fetch_core.sh host|ssp}"

CORE_REF=e702abca17db992a7d62a5108ac99622d002d71b  # v8.1.0, 2026-10-14
CORE_SHA=ce06ca399fdc41a11f2bdc79d48eaf416171c696d6a64217c0c0d11a31a689c1

sha256() { if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1; }

dir="$src/core-$CORE_REF"
if [ ! -d "$dir" ]; then
    rm -rf "$src/.core-part" && mkdir -p "$src/.core-part"
    curl -fsSL "https://github.com/schollz/_core/archive/$CORE_REF.tar.gz" -o "$src/.core-part/archive"
    got=$(sha256 "$src/.core-part/archive")
    if [ "$got" != "$CORE_SHA" ]; then
        echo "_core $CORE_REF: sha256 $got, expected $CORE_SHA" >&2
        exit 1
    fi
    top="_core-$CORE_REF"
    tar xzf "$src/.core-part/archive" -C "$src/.core-part" "$top/lib" "$top/main.c" "$top/rack/src" \
        "$top/LICENSE" "$top/test/rack/fixtures.py" "$top/test/rack/engine_fixture.h"
    mv "$src/.core-part/$top" "$dir"
    rm -rf "$src/.core-part"
fi

case "$target" in
    host) flags=() ;;
    ssp)
        br="${SSP_BUILDROOT:-${BUILDROOT:-$root/buildroot/arm-rockchip-linux-gnueabihf_sdk-buildroot}}"
        sysroot="$br/arm-rockchip-linux-gnueabihf/sysroot"
        [ -d "$sysroot" ] || { echo "no SSP sysroot at $sysroot: run make buildroot" >&2; exit 1; }
        flags=(--target=arm-linux-gnueabihf "--sysroot=$sysroot") ;;
    *) echo "usage: fetch_core.sh host|ssp" >&2; exit 1 ;;
esac

ext="$root/plugins/ezeptocore/Source/ssp_api.inc"
out="$dir/gen/$target"
# regenerate when ssp_api.inc changes; the copy beside the output records what was built
if [ ! -f "$out/engine.c" ] || ! cmp -s "$ext" "$out/ssp_api.inc"; then
    command -v clang >/dev/null || { echo "fetch_core.sh needs clang" >&2; exit 1; }
    rm -rf "$out.part" && mkdir -p "$out.part"
    # prepare.py takes no compiler flags, so a wrapper passes the target's
    printf '#!/bin/sh\nexec clang %s "$@"\n' "${flags[*]:-}" > "$out.part/clang"
    chmod +x "$out.part/clang"
    py=(uv run --quiet --no-project --with numpy python -I "$dir/lib/core_engine/prepare.py" --out "$out.part"
        --clang "$out.part/clang")
    "${py[@]}" --source-only
    cp "$ext" "$dir/lib/core_engine/ssp_api.inc"
    printf '\n#include "ssp_api.inc"\n' >> "$out.part/firmware.c"
    "${py[@]}" --generate-only >/dev/null
    rm -f "$out.part/ast.json" "$out.part/analysis.c" "$out.part/clang"
    cp "$ext" "$out.part/ssp_api.inc"
    rm -rf "$out" && mv "$out.part" "$out"
fi
echo "$dir"
