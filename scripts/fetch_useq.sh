#!/usr/bin/env bash
# Fetch the uSEQ fork (github.com/shakfu/useq, of Emute-Lab-Instruments/uSEQ), whose signal engine the mlsp
# plugin embeds, into build/deps/src/useq-REF: its CMakeLists.txt, src/ and test/.
# Target-independent: CMake compiles it with the plugin's toolchain. The directory names the commit, so
# changing USEQ_REF fetches again.
#
# GitHub's archive tarballs are checked against SHA-256. GitHub has changed their compression before;
# a mismatch after such a change needs the hash updated, not the check removed.
#
# usage: scripts/fetch_useq.sh   (prints the directory)

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src="$root/build/deps/src"

USEQ_REF=1a58d17fc5a68f08433d4312d8d0dcba6cf552cc  # 2026-10-25: upstream b628d0e, the SSP inputs and bound checks, engine source only
USEQ_SHA=aec783a9936a764c7e0cd289aeae873bf085ba0eb3f69e5cbbfbb768fb89adc2

dir="$src/useq-$USEQ_REF"
if [ ! -d "$dir" ]; then
    rm -rf "$src/.useq-part" && mkdir -p "$src/.useq-part"
    curl -fsSL "https://github.com/shakfu/useq/archive/$USEQ_REF.tar.gz" -o "$src/.useq-part/archive"
    got=$(if command -v sha256sum >/dev/null; then sha256sum "$src/.useq-part/archive"; else
        shasum -a 256 "$src/.useq-part/archive"; fi | cut -d' ' -f1)
    if [ "$got" != "$USEQ_SHA" ]; then
        echo "uSEQ $USEQ_REF: sha256 $got, expected $USEQ_SHA" >&2
        exit 1
    fi
    tar xzf "$src/.useq-part/archive" -C "$src/.useq-part" "useq-$USEQ_REF/CMakeLists.txt" "useq-$USEQ_REF/src" "useq-$USEQ_REF/test"
    mv "$src/.useq-part/useq-$USEQ_REF" "$dir"
    rm -rf "$src/.useq-part"
fi
echo "$dir"
