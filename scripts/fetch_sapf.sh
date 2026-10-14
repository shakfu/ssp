#!/usr/bin/env bash
# Fetch sapf-sc, the interpreter the sapf plugin embeds, into build/deps/src/sapf-sc-REF.
# Target-independent: CMake compiles it with the plugin's toolchain. The directory names the
# commit, so changing SAPF_REF fetches again.
#
# GitHub's archive tarballs are checked against SHA-256. GitHub has changed their compression before;
# a mismatch after such a change needs the hash updated, not the check removed.
#
# usage: scripts/fetch_sapf.sh   (prints the directory)

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src="$root/build/deps/src"

SAPF_REF=06838b209b38b54da9f7e08de1e416efddb6bf52  # 2026-10-14, "more bug fixes": interrupt, runningTextures
SAPF_SHA=5b4f77391fcb60675073d37d26ee199fdb8b9c2f1d505e8a97605a05f56c3ee7

dir="$src/sapf-sc-$SAPF_REF"
if [ ! -d "$dir" ]; then
    rm -rf "$src/.sapf-part" && mkdir -p "$src/.sapf-part"
    curl -fsSL "https://github.com/shakfu/sapf-sc/archive/$SAPF_REF.tar.gz" -o "$src/.sapf-part/archive"
    got=$(if command -v sha256sum >/dev/null; then sha256sum "$src/.sapf-part/archive"; else
        shasum -a 256 "$src/.sapf-part/archive"; fi | cut -d' ' -f1)
    if [ "$got" != "$SAPF_SHA" ]; then
        echo "sapf-sc $SAPF_REF: sha256 $got, expected $SAPF_SHA" >&2
        exit 1
    fi
    tar xzf "$src/.sapf-part/archive" -C "$src/.sapf-part"
    mv "$src/.sapf-part/sapf-sc-$SAPF_REF" "$dir"
    rm -rf "$src/.sapf-part"
fi
echo "$dir"
