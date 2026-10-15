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

SAPF_REF=09e809884d0eec93ff7d888b631e2eb205133b84  # 2026-10-15, "bug fixes in linux": MaxMSPSetInputLead
SAPF_SHA=88f88f1cff9509fccc0baab2aaf539ce8808f4183517d55e60ae6df1c7467431

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
