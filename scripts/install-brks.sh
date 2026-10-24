#!/usr/bin/env bash
# copy _core's default sample banks (96 samples in 6 banks, from the release its sample manager downloads)
# to the SSP's SD card (ssp-card.sh: mounted, or over ssh) as BOOT/brks/default, which a new brks loads. Only the card files
# are copied: bank1-bank6, settings/ and the collection's README. The editor project (.core-manager) is not.
#
# The ZIP (127 MB) is cached in build/deps/src and checked against the SHA-256 _core's sample manager pins
# (sample-manager/Source/DefaultSamples.cpp at v8.1.0).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST=brks/default  # on the card
URL=https://github.com/schollz/_core/releases/download/default-samples/1-ezeptocore-default-banks.zip
SHA=1f45e8cb7049dd4dc27e1e6f2df247ab254a71f03e604f6549b577de5fb8fc30
ZIP="$ROOT/build/deps/src/1-ezeptocore-default-banks.zip"
MARK=.brks-default  # marks a folder this script wrote, so it may replace it

sha256() { if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1; }

source "$ROOT/scripts/ssp-card.sh"
if [ "$(card_sh "[ ! -e $DEST ] || [ -e $DEST/$MARK ] && echo ok")" != ok ]; then
    echo "$CARD_WHERE/$DEST exists and was not written by this script; move it first" >&2
    exit 1
fi

if [ ! -f "$ZIP" ] || [ "$(sha256 "$ZIP")" != "$SHA" ]; then
    mkdir -p "$(dirname "$ZIP")"
    curl -fSL "$URL" -o "$ZIP.part"
    got=$(sha256 "$ZIP.part")
    [ "$got" = "$SHA" ] || { echo "$URL: sha256 $got, expected $SHA" >&2; rm -f "$ZIP.part"; exit 1; }
    mv "$ZIP.part" "$ZIP"
fi

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
unzip -q "$ZIP" 'bank*/*' 'settings/*' README.md -d "$stage"
touch "$stage/$MARK"
card_tree "$stage" "$DEST"
card_sync
echo "$CARD_WHERE/$DEST: $(card_sh "ls -d $DEST/bank* | wc -l") banks"
