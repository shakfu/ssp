#!/usr/bin/env bash
# Fetch and build the static, position-independent libraries the csound and chuck plugins link, into
# build/deps/<target>: libsndfile, Csound 7 (float samples) and the ChucK core. Target "ssp"
# cross-compiles with the buildroot; "host" builds for the native tests.
#
# libsndfile is built here because the buildroot's libsndfile.a is not PIC, and linking its .so
# would stop the plugins loading on a card without it.
#
# usage: scripts/build_deps.sh ssp|host

set -euo pipefail

target="${1:?usage: $0 ssp|host}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src="$root/build/deps/src"
prefix="$root/build/deps/$target"
jobs="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu)}"

SNDFILE_REF="${SNDFILE_REF:-1.2.2}"
CSOUND_REF="${CSOUND_REF:-7.0.0-beta.17}"
CHUCK_REF="${CHUCK_REF:-chuck-1.5.5.8}"

case "$target" in
    ssp)
        br="${SSP_BUILDROOT:-${BUILDROOT:-$root/buildroot/arm-rockchip-linux-gnueabihf_sdk-buildroot}}"
        sysroot="$br/arm-rockchip-linux-gnueabihf/sysroot"
        cxxinc="$br/arm-rockchip-linux-gnueabihf/include/c++/8.4.0"
        toolchain=(-DCMAKE_TOOLCHAIN_FILE="$root/xcSSP.cmake")
        cpu="--target=arm-linux-gnueabihf --sysroot=$sysroot -mcpu=cortex-a17 -mfloat-abi=hard -mfpu=neon-vfpv4"
        cc="clang $cpu"
        cxx="clang++ $cpu -I$cxxinc -I$cxxinc/arm-rockchip-linux-gnueabihf"
        ;;
    host)
        toolchain=()
        cc="${CC:-cc}"
        cxx="${CXX:-c++}"
        ;;
    *) echo "unknown target $target" >&2; exit 2 ;;
esac

fetch() {  # url dir
    [ -d "$src/$2" ] && return
    rm -rf "$src/.part" && mkdir -p "$src/.part"
    curl -fsSL "$1" -o "$src/.part/archive"
    tar xf "$src/.part/archive" -C "$src/.part"
    mv "$src/.part/$2" "$src/$2"
    rm -rf "$src/.part"
}

fetch "https://github.com/libsndfile/libsndfile/releases/download/$SNDFILE_REF/libsndfile-$SNDFILE_REF.tar.xz" \
    "libsndfile-$SNDFILE_REF"
fetch "https://github.com/csound/csound/archive/refs/tags/$CSOUND_REF.tar.gz" "csound-$CSOUND_REF"
fetch "https://github.com/ccrma/chuck/archive/refs/tags/$CHUCK_REF.tar.gz" "chuck-$CHUCK_REF"

common=(-DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_INSTALL_PREFIX="$prefix"
        -DCMAKE_PREFIX_PATH="$prefix" -DCMAKE_FIND_ROOT_PATH="$prefix" -DBUILD_SHARED_LIBS=OFF)

# WAV, AIFF and the other built-in formats; no external codecs
cmake -S "$src/libsndfile-$SNDFILE_REF" -B "$prefix/build/libsndfile" "${toolchain[@]}" "${common[@]}" \
    -DENABLE_EXTERNAL_LIBS=OFF -DENABLE_MPEG=OFF -DBUILD_PROGRAMS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF \
    -DENABLE_CPACK=OFF -DINSTALL_MANPAGES=OFF -DENABLE_PACKAGE_CONFIG=OFF
cmake --build "$prefix/build/libsndfile" -j"$jobs"
cmake --install "$prefix/build/libsndfile"

# float samples, matching the plugin; no plugins, utilities or optional libraries
cmake -S "$src/csound-$CSOUND_REF" -B "$prefix/build/csound" "${toolchain[@]}" "${common[@]}" \
    -DUSE_DOUBLE=OFF -DBUILD_STATIC_LIBRARY=ON -DBUILD_UTILITIES=OFF -DBUILD_TESTS=OFF -DBUILD_PLUGINS=OFF \
    -DBUILD_PERFTHREAD_CLASS=OFF -DUSE_GETTEXT=OFF -DUSE_CURL=OFF -DUSE_LIBSAMPLERATE=OFF -DUSE_GIT_COMMIT=OFF \
    -DUSE_ASA=OFF -DUSE_AVX2=OFF -DUSE_DEFAULT_OPCODEDIR=OFF -DUSE_MP3=OFF -DUSE_VCPKG=OFF -DINSTALL_PYTHON_INTERFACE=OFF \
    -DSndFile_INCLUDE_DIR="$prefix/include" -DSndFile_LIBRARY="$prefix/lib/libsndfile.a"
cmake --build "$prefix/build/csound" -j"$jobs"
cmake --install "$prefix/build/csound"

# ChucK's own makefile builds the core objects in place, so each target gets a copy of the sources.
# CHUCK_DEFS must match plugins/chuck/CMakeLists.txt: they change ChucK's class layouts.
CHUCK_DEFS="-D__LINUX_ALSA__ -D__CK_SNDFILE_NATIVE__"
ck="$prefix/build/chuck"
[ -d "$ck" ] || cp -r "$src/chuck-$CHUCK_REF/src/core" "$ck"
CFLAGS="-fPIC $CHUCK_DEFS -I$prefix/include" make -C "$ck" -j"$jobs" linux-alsa CC="$cc" CXX="$cxx" \
    CHUCK_STRICT= >/dev/null
rm -f "$prefix/lib/libchuck.a"
ar rcs "$prefix/lib/libchuck.a" "$ck"/*.o "$ck"/lo/*.o
mkdir -p "$prefix/include/chuck/lo"
cp "$ck"/*.h "$prefix/include/chuck/"
cp "$ck"/lo/*.h "$prefix/include/chuck/lo/"
echo "deps for $target in $prefix"
