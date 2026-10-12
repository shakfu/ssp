#!/usr/bin/env bash
# Fetch the sources the teletype plugin builds its firmware from, into build/deps/src/teletype:
# monome teletype and libavr32, and monome-rack's mock hardware layer. Generates the firmware's two
# ragel scanners, building ragel 6 when it is not installed. Target-independent: CMake compiles the
# firmware with the plugin's toolchain.
#
# GitHub's archive tarballs are checked against SHA-256. GitHub has changed their compression before;
# a mismatch after such a change needs the hashes updated, not the check removed.
#
# usage: scripts/fetch_teletype.sh

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest="$root/build/deps/src/teletype"
jobs="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu)}"

TELETYPE_REF=e390bacdf024f2234e2ade6a4821773b9a80a322     # 2026-06-24, "keyboard fix (#345)"
TELETYPE_SHA=599f8fdb751ed891c6ac4f23de0e5c2481fbef006da0dc85e2aa0c79e9539fee
LIBAVR32_REF=b37dc3f6c67a796072ae9196551938c9d2863875     # teletype's submodule at TELETYPE_REF
LIBAVR32_SHA=801785644fef6c639808934944b79d9fbba2067a36129cf3620342b0cc363282
MONOME_RACK_REF=1f5a9ca734a1dbe54db3076f9d6ace806bd02629  # 2025-01-27
MONOME_RACK_SHA=313ed4758d88beff9b78fd74e491e9b3e17fb44272b6dc9338a7fd8257e0d663
RAGEL_VERSION=6.10
RAGEL_SHA=5f156edb65d20b856d638dd9ee2dfb43285914d9aa2b6ec779dac0270cd56c3f

sha256() { if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1; }

fetch() {  # url sha dir; extracts the archive's single top-level directory as $dest/dir
    [ -d "$dest/$3" ] && return
    rm -rf "$dest/.part" && mkdir -p "$dest/.part"
    curl -fsSL "$1" -o "$dest/.part/archive"
    local got
    got=$(sha256 "$dest/.part/archive")
    if [ "$got" != "$2" ]; then
        echo "$1: sha256 $got, expected $2" >&2
        exit 1
    fi
    tar xzf "$dest/.part/archive" -C "$dest/.part"
    rm "$dest/.part/archive"
    mv "$dest/.part"/* "$dest/$3"
    rm -rf "$dest/.part"
}

mkdir -p "$dest"
fetch "https://github.com/monome/teletype/archive/$TELETYPE_REF.tar.gz" "$TELETYPE_SHA" teletype
fetch "https://github.com/monome/libavr32/archive/$LIBAVR32_REF.tar.gz" "$LIBAVR32_SHA" libavr32
fetch "https://github.com/Dewb/monome-rack/archive/$MONOME_RACK_REF.tar.gz" "$MONOME_RACK_SHA" monome-rack

# mock_hardware_api.h includes serializer.h through monome-rack's teletype4 submodule; teletype 5's
# differs only in parameter names
mkdir -p "$dest/monome-rack/firmware/teletype4/src"
ln -sfn "$dest/teletype/src/serializer.h" "$dest/monome-rack/firmware/teletype4/src/serializer.h"

ragel=$(command -v ragel || true)
if [ -z "$ragel" ]; then
    ragel="$dest/ragel/bin/ragel"
    if [ ! -x "$ragel" ]; then
        fetch "https://www.colm.net/files/ragel/ragel-$RAGEL_VERSION.tar.gz" "$RAGEL_SHA" "ragel-$RAGEL_VERSION"
        (cd "$dest/ragel-$RAGEL_VERSION" && ./configure --prefix="$dest/ragel" --disable-manual >/dev/null &&
            make -j"$jobs" >/dev/null 2>&1 && make install >/dev/null)
    fi
fi
for f in scanner match_token; do
    out="$dest/teletype/src/$f.c"
    [ "$out" -nt "$dest/teletype/src/$f.rl" ] || "$ragel" -C -G2 "$dest/teletype/src/$f.rl" -o "$out"
done
echo "teletype sources in $dest"
