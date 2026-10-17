#!/usr/bin/env bash
# Fetch psnd's Alda implementation (github.com/shakfu/psnd, source/langs/alda), which the alda plugin
# embeds, into build/deps/src/alda-REF. Only that directory is kept. Target-independent: CMake compiles it
# with the plugin's toolchain. The directory names the commit, so changing ALDA_REF fetches again.
#
# GitHub's archive tarballs are checked against SHA-256. GitHub has changed their compression before;
# a mismatch after such a change needs the hash updated, not the check removed.
#
# usage: scripts/fetch_alda.sh   (prints the directory)

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src="$root/build/deps/src"

ALDA_REF=421cf5a9efdcbeaf53a27aec42d8594b492c56f1  # psnd, 2026-10-13
ALDA_SHA=3f3b676bae613e080bab0e031edb7d58baf97b86acc9edffef32dc27108401e0

dir="$src/alda-$ALDA_REF"
if [ ! -d "$dir" ]; then
    rm -rf "$src/.alda-part" && mkdir -p "$src/.alda-part"
    curl -fsSL "https://github.com/shakfu/psnd/archive/$ALDA_REF.tar.gz" -o "$src/.alda-part/archive"
    got=$(if command -v sha256sum >/dev/null; then sha256sum "$src/.alda-part/archive"; else
        shasum -a 256 "$src/.alda-part/archive"; fi | cut -d' ' -f1)
    if [ "$got" != "$ALDA_SHA" ]; then
        echo "psnd $ALDA_REF: sha256 $got, expected $ALDA_SHA" >&2
        exit 1
    fi
    tar xzf "$src/.alda-part/archive" -C "$src/.alda-part" "psnd-$ALDA_REF/source/langs/alda"
    mv "$src/.alda-part/psnd-$ALDA_REF/source/langs/alda" "$dir"
    rm -rf "$src/.alda-part"
fi
echo "$dir"
