#!/usr/bin/env bash
# Fetch and build the static, position-independent libraries the csound, chuck, faust and scsynth
# plugins link, into build/deps/<target>: libsndfile, Csound 7 (float samples), the ChucK core, libfaust
# (LLVM and interpreter backends), and libscsynth with SC's and sc3-plugins' UGens as .so files.
# Target "ssp" cross-compiles with the buildroot; "host" builds for the native tests.
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
FAUST_REF="${FAUST_REF:-2.85.9}"  # the version scripts/faust_kernel.sh compiles with
SC_REF="${SC_REF:-3.14.1}"  # SuperCollider and sc3-plugins: one version, so the plugin API matches
FFTW_REF="${FFTW_REF:-3.3.10}"

case "$target" in
    ssp)
        br="${SSP_BUILDROOT:-${BUILDROOT:-$root/buildroot/arm-rockchip-linux-gnueabihf_sdk-buildroot}}"
        sysroot="$br/arm-rockchip-linux-gnueabihf/sysroot"
        cxxinc="$br/arm-rockchip-linux-gnueabihf/include/c++/8.4.0"
        toolchain=(-DCMAKE_TOOLCHAIN_FILE="$root/xcSSP.cmake")
        cpu="--target=arm-linux-gnueabihf --sysroot=$sysroot -mcpu=cortex-a17 -mfloat-abi=hard -mfpu=neon-vfpv4"
        cc="clang $cpu"
        cxx="clang++ $cpu -I$cxxinc -I$cxxinc/arm-rockchip-linux-gnueabihf"
        # the SSP's own LLVM: its rootfs has the same libLLVM-9.so, which Mesa's drivers link
        llvm_version=$(sed -n 's/^#define LLVM_VERSION_STRING "\(.*\)"/\1/p' "$sysroot/usr/include/llvm/Config/llvm-config.h")
        llvm_include="$sysroot/usr/include"
        llvm_lib="$sysroot/usr/lib"
        ;;
    host)
        toolchain=()
        cc="${CC:-cc}"
        cxx="${CXX:-c++}"
        llvm_version=$(llvm-config --version)
        llvm_include=$(llvm-config --includedir)
        llvm_lib=$(llvm-config --libdir)
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
fetch "https://github.com/grame-cncm/faust/releases/download/$FAUST_REF/faust-$FAUST_REF.tar.gz" "faust-$FAUST_REF"
sc_url="https://github.com/supercollider"
fetch "$sc_url/supercollider/releases/download/Version-$SC_REF/SuperCollider-$SC_REF-Source.tar.bz2" \
    "SuperCollider-$SC_REF-Source"
fetch "$sc_url/sc3-plugins/releases/download/Version-$SC_REF/sc3-plugins-$SC_REF-Source.tar.bz2" \
    "sc3-plugins-$SC_REF-Source"
fetch "https://www.fftw.org/fftw-$FFTW_REF.tar.gz" "fftw-$FFTW_REF"

apply_patch() {  # dir patch; applied once: a reverse dry run succeeds when it is already in
    patch -d "$1" -p1 -R -s -f --dry-run <"$2" >/dev/null || patch -d "$1" -p1 -s <"$2"
}
apply_patch "$src/faust-$FAUST_REF" "$root/scripts/patches/faust-$FAUST_REF-jit-target.patch"
apply_patch "$src/SuperCollider-$SC_REF-Source" "$root/scripts/patches/supercollider-$SC_REF-threads.patch"
apply_patch "$src/sc3-plugins-$SC_REF-Source" "$root/scripts/patches/sc3-plugins-$SC_REF-libstdcxx.patch"

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
# libfaust with the LLVM and interpreter backends; no compiler executable, no OSC or HTTP. LLVM is
# not merged into the archive: plugins link the shared libLLVM. Faust writes the archive into its
# source tree, under LIBSDIR, so each target gets its own.
cmake -S "$src/faust-$FAUST_REF/build" -B "$prefix/build/faust" "${toolchain[@]}" "${common[@]}" \
    -C "$src/faust-$FAUST_REF/build/backends/interp.cmake" -DCPP_BACKEND=OFF -DLLVM_BACKEND=STATIC \
    -DINCLUDE_STATIC=ON -DINCLUDE_DYNAMIC=OFF -DINCLUDE_EXECUTABLE=OFF -DINCLUDE_OSC=OFF -DINCLUDE_HTTP=OFF \
    -DINCLUDE_EMCC=OFF -DINCLUDE_WASM_GLUE=OFF -DSELF_CONTAINED_LIBRARY=ON -DLIBSDIR="lib-$target" \
    -DINCLUDE_LLVM_STATIC_IN_ARCHIVE=OFF -DUSE_LLVM_CONFIG=OFF -DLLVM_PACKAGE_VERSION="$llvm_version" \
    -DLLVM_INCLUDE_DIRS="$llvm_include" -DLLVM_LIB_DIR="$llvm_lib" -DLLVM_LIBS="-lLLVM" \
    -DLLVM_DEFINITIONS="-D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS"
cmake --build "$prefix/build/faust" -j"$jobs" --target staticlib
cp "$src/faust-$FAUST_REF/build/lib-$target/libfaust.a" "$prefix/lib/"
rm -rf "$prefix/include/faust"
cp -r "$src/faust-$FAUST_REF/architecture/faust" "$prefix/include/faust"
rm -rf "$prefix/share/faust" && mkdir -p "$prefix/share"
cp -r "$src/faust-$FAUST_REF/libraries" "$prefix/share/faust"  # stdfaust.lib and the rest, for programs

# libscsynth without a driver, and SC's core UGens; see scripts/scsynth/CMakeLists.txt
sc="$src/SuperCollider-$SC_REF-Source"
cmake -S "$root/scripts/scsynth" -B "$prefix/build/scsynth" "${toolchain[@]}" "${common[@]}" \
    -DSC_PATH="$sc" -DSNDFILE_PREFIX="$prefix"
cmake --build "$prefix/build/scsynth" -j"$jobs"
cmake --install "$prefix/build/scsynth"
# the module includes scsynth's internal headers from the source tree, by a path without the version
ln -sfn "$sc" "$prefix/include/supercollider"
# single-precision FFTW for sc3-plugins' PitchDetection and NCAnalysisUGens
cmake -S "$src/fftw-$FFTW_REF" -B "$prefix/build/fftw" "${toolchain[@]}" "${common[@]}" \
    -DENABLE_FLOAT=ON -DBUILD_TESTS=OFF -DDISABLE_FORTRAN=ON
cmake --build "$prefix/build/fftw" -j"$jobs"
cmake --install "$prefix/build/fftw"
# sc3-plugins, staged then copied beside the core UGens; the .sc class files are for sclang, not here
cmake -S "$src/sc3-plugins-$SC_REF-Source" -B "$prefix/build/sc3-plugins" "${toolchain[@]}" "${common[@]}" \
    -DCMAKE_INSTALL_PREFIX="$prefix/build/sc3-plugins-stage" -DSC_PATH="$sc" -DSUPERNOVA=OFF -DLADSPA=OFF \
    -DIN_PLACE_BUILD=OFF -DQUARKS=OFF -DUSE_CCACHE=OFF -DFFTW3F_INCLUDE_DIR="$prefix/include" \
    -DFFTW3F_LIBRARY="$prefix/lib/libfftw3f.a" -DCMAKE_MODULE_LINKER_FLAGS="-Wl,--exclude-libs,ALL"
cmake --build "$prefix/build/sc3-plugins" -j"$jobs"
cmake --install "$prefix/build/sc3-plugins"
rm -rf "$prefix/scsynth/plugins/sc3-plugins"
cp -r "$prefix/build/sc3-plugins-stage/lib/SuperCollider/plugins" "$prefix/scsynth/plugins/sc3-plugins"
echo "deps for $target in $prefix"
