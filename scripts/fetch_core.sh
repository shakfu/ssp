#!/usr/bin/env bash
# Fetch schollz/_core, the firmware the brks plugin runs, into build/deps/src/core-REF, and
# generate its desktop engine for TARGET (host or ssp) into core-REF/gen/TARGET/engine.c. Only lib/ and
# main.c, which _core's generator reads, rack/src (its card reader) and the licence are kept.
#
# The generator, _core's lib/core_engine/prepare.py, needs clang (its JSON AST) and numpy, run here
# through uv. It moves the firmware's globals into a per-instance struct; the result includes the
# target's preprocessed libc, so each target gets its own. plugins/breaks/Source/ssp_api.inc joins
# the firmware source between prepare.py's two stages, so its functions are rewritten with the rest;
# a call into it is added to input_handling's loop (docs/dev/breaks.md, Mult and Bank).
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

ext="$root/plugins/breaks/Source/ssp_api.inc"
out="$dir/gen/$target"
# regenerate when ssp_api.inc or this script changes; the copies beside the output record what was built
if [ ! -f "$out/engine.c" ] || ! cmp -s "$ext" "$out/ssp_api.inc" || ! cmp -s "$0" "$out/fetch_core.sh"; then
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
    # ssp_api.inc's hook into input_handling's loop, beside prepare.py's own (host_selection_changed)
    uv run --quiet --no-project python -I - "$out.part/firmware.c" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
fn = "void __not_in_flash_func(input_handling)() {"
hook = "    if(host_selection_changed){"
assert s.count(fn) == 1 and s.count(hook) == 1, "firmware.c: input_handling changed upstream"
s = s.replace(fn, "static void ssp_input_handling(uint8_t*,uint8_t*,SampleCVState*);\n" + fn)
i = s.index("\n", s.index(hook)) + 1
s = s[:i] + "    ssp_input_handling(&sel_bank_next_new,&sel_sample_next_new,&sample_cv_state);\n" + s[i:]
open(p, "w").write(s)
PY
    "${py[@]}" --generate-only >/dev/null
    rm -f "$out.part/ast.json" "$out.part/analysis.c" "$out.part/clang"
    cp "$ext" "$out.part/ssp_api.inc"
    cp "$0" "$out.part/fetch_core.sh"
    rm -rf "$out" && mv "$out.part" "$out"
fi
echo "$dir"
