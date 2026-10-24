#!/usr/bin/env bash
# Fetch the sapf fork (github.com/shakfu/sapf) the sapf plugin embeds, into build/deps/src/sapf-REF.
# Target-independent: CMake compiles it with the plugin's toolchain. The directory names the commit, so
# changing SAPF_REF fetches again.
#
# GitHub's archive tarballs are checked against SHA-256. GitHub has changed their compression before;
# a mismatch after such a change needs the hash updated, not the check removed.
#
# usage: scripts/fetch_sapf.sh   (prints the directory)

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src="$root/build/deps/src"

SAPF_REF=0c8a3a443eae29fe73d8b7828b103087b31bd048  # 2026-10-24, "fix mouse-related words": SapfSetMouse
SAPF_SHA=ea344bac63a78eafa7f3241e3534345a6e2d8bce819f859b2fae4019b6d36eab

dir="$src/sapf-$SAPF_REF"
if [ ! -d "$dir" ]; then
    rm -rf "$src/.sapf-part" && mkdir -p "$src/.sapf-part"
    curl -fsSL "https://github.com/shakfu/sapf/archive/$SAPF_REF.tar.gz" -o "$src/.sapf-part/archive"
    got=$(if command -v sha256sum >/dev/null; then sha256sum "$src/.sapf-part/archive"; else
        shasum -a 256 "$src/.sapf-part/archive"; fi | cut -d' ' -f1)
    if [ "$got" != "$SAPF_SHA" ]; then
        echo "sapf $SAPF_REF: sha256 $got, expected $SAPF_SHA" >&2
        exit 1
    fi
    tar xzf "$src/.sapf-part/archive" -C "$src/.sapf-part"
    mv "$src/.sapf-part/sapf-$SAPF_REF" "$dir"
    rm -rf "$src/.sapf-part"
fi
echo "$dir"
