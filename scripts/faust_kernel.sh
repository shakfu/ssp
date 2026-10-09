#!/usr/bin/env bash
# Compiles a Faust .dsp into a kernel header for FaustEngine: `class mydsp` in namespace ssp::faust::<ns>.
# usage: scripts/faust_kernel.sh <file.dsp> <out.h> <ns>
set -euo pipefail

[ $# -eq 3 ] || { echo "usage: $0 <file.dsp> <out.h> <ns>" >&2; exit 2; }
dsp=$1 out=$2 ns=$3
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT

# PYTHONUTF8: the Faust libraries' metadata hold non-ASCII names, which cyfaust writes as text
PYTHONUTF8=1 uv run --quiet --no-project --with cyfaust==0.2.0 python -m cyfaust compile "$dsp" -b cpp -o "$tmp" >/dev/null

{
    echo "// Generated from $(basename "$dsp") by scripts/faust_kernel.sh; do not edit."
    echo "#pragma once"
    echo
    # the kernel's own includes, moved outside the namespace
    grep '^#include' "$tmp"
    echo
    echo "#include \"engine/FaustArch.h\""
    echo
    echo "namespace ssp::faust::$ns {"
    grep -v '^#include' "$tmp" | iconv -f utf-8 -t ascii//TRANSLIT
    echo "}  // namespace ssp::faust::$ns"
} >"$out"
