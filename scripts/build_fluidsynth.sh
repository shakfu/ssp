#!/usr/bin/env bash
# Build FluidSynth as a static, position-independent library for the fsyn plugin, into build/deps/<target>,
# and fetch into build/deps/src FluidR3_GM.sf2, the soundfont fsyn ships, and TimGM6mb.sf2, which its tests
# load (6 MB, 60 ms). No audio or MIDI drivers and no
# libsndfile, so SF2 only: SF3 needs libsndfile with Ogg/Vorbis. Floats, not FluidSynth's default doubles.
# Target "ssp" cross-compiles with the buildroot; "host" builds for the native tests.
#
# Downloads are checked against SHA-256. GitHub has changed its archive compression before; a mismatch
# after such a change needs the hash updated, not the check removed.
#
# usage: scripts/build_fluidsynth.sh ssp|host

set -euo pipefail

target="${1:?usage: $0 ssp|host}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src="$root/build/deps/src"
prefix="$root/build/deps/$target"
jobs="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu)}"

FLUIDSYNTH_REF=v2.6.1
FLUIDSYNTH_SHA=3d258a3bf97cc20c59eeebfe62c2432fae88adda74d3ad098681c76e0ebf446b
GCEM_REF=012ae73c6d0a2cb09ffe86475f5c6fba3926e200  # FluidSynth's gcem submodule at FLUIDSYNTH_REF
GCEM_SHA=34ab0ee87a9eb26d3087fa9b49c2572ea8ee03db0c9705b83648301a3a3fc172
SF_URL=https://github.com/shakfu/aldakit/releases/download/soundfonts-v1  # aldakit's copies, unmodified
FLUIDR3_SHA=74594e8f4250680adf590507a306655a299935343583256f3b722c48a1bc1cb0
TIMGM6MB_SHA=82475b91a76de15cb28a104707d3247ba932e228bada3f47bba63c6b31aaf7a1

case "$target" in
    # GCC 8's std::filesystem is in libstdc++fs, which fsyn links too; without it FluidSynth stubs its file tests
    ssp)
        br="${SSP_BUILDROOT:-${BUILDROOT:-$root/buildroot/arm-rockchip-linux-gnueabihf_sdk-buildroot}}"
        fslib="$br/arm-rockchip-linux-gnueabihf/lib/libstdc++fs.a"
        toolchain=(-DCMAKE_TOOLCHAIN_FILE="$root/xcSSP.cmake" -DCMAKE_REQUIRED_LIBRARIES="$fslib"
                   -DCMAKE_CXX_STANDARD_LIBRARIES="$fslib") ;;
    host) toolchain=() ;;
    *) echo "unknown target $target" >&2; exit 2 ;;
esac

check() {  # file sha
    local got
    got=$(if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1)
    [ "$got" = "$2" ] || { echo "$1: sha256 $got, expected $2" >&2; exit 1; }
}

fetch() {  # url sha dir: a tarball whose one top directory becomes $src/dir
    [ -d "$src/$3" ] && return
    rm -rf "$src/.part" && mkdir -p "$src/.part/x"
    curl -fsSL "$1" -o "$src/.part/archive"
    check "$src/.part/archive" "$2"
    tar xzf "$src/.part/archive" -C "$src/.part/x"
    mv "$src/.part/x"/* "$src/$3"
    rm -rf "$src/.part"
}

fs="fluidsynth-${FLUIDSYNTH_REF#v}"
fetch "https://github.com/FluidSynth/fluidsynth/archive/refs/tags/$FLUIDSYNTH_REF.tar.gz" "$FLUIDSYNTH_SHA" "$fs"
# the tarball has an empty gcem/; FluidSynth's configure would otherwise download it
if [ ! -f "$src/$fs/gcem/include/gcem.hpp" ]; then
    fetch "https://github.com/kthohr/gcem/archive/$GCEM_REF.tar.gz" "$GCEM_SHA" "gcem-$GCEM_REF"
    rm -rf "$src/$fs/gcem" && cp -r "$src/gcem-$GCEM_REF" "$src/$fs/gcem"
fi
for sf in FluidR3_GM:$FLUIDR3_SHA TimGM6mb:$TIMGM6MB_SHA; do
    name="${sf%%:*}.sf2"
    [ -f "$src/$name" ] && continue
    curl -fsSL "$SF_URL/$name" -o "$src/.$name.part"
    check "$src/.$name.part" "${sf#*:}"
    mv "$src/.$name.part" "$src/$name"
done

off=()
for o in alsa aufile dbus ipv6 jack ladspa libsndfile midishare network oss sdl3 pulseaudio pipewire portaudio \
         readline systemd openmp threads signalsmith; do
    off+=("-Denable-$o=OFF")
done
cmake -S "$src/$fs" -B "$prefix/build/fluidsynth" "${toolchain[@]}" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_INSTALL_PREFIX="$prefix" -DBUILD_SHARED_LIBS=OFF \
    -Dosal=cpp11 -Denable-floats=ON "${off[@]}" >/dev/null
cmake --build "$prefix/build/fluidsynth" -j"$jobs" >/dev/null
cmake --install "$prefix/build/fluidsynth" >/dev/null
echo "$prefix/lib/libfluidsynth.a"
