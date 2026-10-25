#!/usr/bin/env bash
# Build libmonome, for monome grids and arcs, as static, position-independent libraries into
# build/deps/<target>, with its libftdi backend: the SSP's kernel has no driver for the FTDI chip in monome
# devices, so libmonome reaches them through libftdi1 and libusb-1.0 (usbfs). libusb is built without udev.
# libmonome comes from the fork github.com/shakfu/libmonome, which adds the backend, at LIBMONOME_REF, or from
# a checkout at LIBMONOME_DIR. Target "ssp" cross-compiles with the buildroot; "host" builds for native tests.
#
# Downloads are checked against SHA-256. GitHub has changed its archive compression before; a mismatch
# after such a change needs the hash updated, not the check removed.
#
# usage: scripts/build_monome.sh ssp|host

set -euo pipefail

target="${1:?usage: $0 ssp|host}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src="$root/build/deps/src"
prefix="$root/build/deps/$target"
jobs="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu)}"

LIBUSB_REF=1.0.27
LIBUSB_SHA=ffaa41d741a8a3bee244ac8e54a72ea05bf2879663c098c82fc5757853441575
LIBFTDI_REF=1.5
LIBFTDI_SHA=7c7091e9c86196148bd41177b4590dccb1510bfe6cea5bf7407ff194482eb049

case "$target" in
    ssp)
        br="${SSP_BUILDROOT:-${BUILDROOT:-$root/buildroot/arm-rockchip-linux-gnueabihf_sdk-buildroot}}"
        sysroot="$br/arm-rockchip-linux-gnueabihf/sysroot"
        toolchain=(-DCMAKE_TOOLCHAIN_FILE="$root/xcSSP.cmake")
        host=(--host=arm-linux-gnueabihf)
        cc="clang --target=arm-linux-gnueabihf --sysroot=$sysroot -mcpu=cortex-a17 -mfloat-abi=hard -mfpu=neon-vfpv4"
        gccroot="$br/lib/gcc/arm-rockchip-linux-gnueabihf/8.4.0"
        ldflags="-fuse-ld=lld -L$sysroot/lib -B$sysroot/lib -L$gccroot -B$gccroot"  # as xcSSP.cmake links
        ;;
    host) toolchain=(); host=(); cc="${CC:-cc}"; ldflags="" ;;
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
    tar xf "$src/.part/archive" -C "$src/.part/x"
    mv "$src/.part/x"/* "$src/$3"
    rm -rf "$src/.part"
}

fetch "https://github.com/libusb/libusb/releases/download/v$LIBUSB_REF/libusb-$LIBUSB_REF.tar.bz2" "$LIBUSB_SHA" \
    "libusb-$LIBUSB_REF"
fetch "https://www.intra2net.com/en/developer/libftdi/download/libftdi1-$LIBFTDI_REF.tar.bz2" "$LIBFTDI_SHA" \
    "libftdi1-$LIBFTDI_REF"
monome="${LIBMONOME_DIR:?set LIBMONOME_DIR to a checkout of github.com/shakfu/libmonome}"

# autotools, in a build directory per target; no udev: libmonome names the device, libusb need not find it
usb="$prefix/build/libusb"
mkdir -p "$usb"
if [ ! -f "$usb/Makefile" ]; then
    (cd "$usb" && "$src/libusb-$LIBUSB_REF/configure" "${host[@]}" --prefix="$prefix" --enable-static \
        --disable-shared --with-pic --disable-udev CC="$cc" LDFLAGS="$ldflags" AR=llvm-ar RANLIB=llvm-ranlib \
        >/dev/null)
fi
make -C "$usb" -j"$jobs" >/dev/null
make -C "$usb" install >/dev/null

common=(-DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_INSTALL_PREFIX="$prefix"
        -DCMAKE_PREFIX_PATH="$prefix" -DCMAKE_FIND_ROOT_PATH="$prefix" -DBUILD_SHARED_LIBS=OFF)

# the C library only, static
cmake -S "$src/libftdi1-$LIBFTDI_REF" -B "$prefix/build/libftdi1" "${toolchain[@]}" "${common[@]}" \
    -DSTATICLIBS=ON -DFTDIPP=OFF -DFTDI_EEPROM=OFF -DEXAMPLES=OFF -DPYTHON_BINDINGS=OFF -DDOCUMENTATION=OFF \
    -DBUILD_TESTS=OFF -DLIBUSB_INCLUDE_DIR="$prefix/include/libusb-1.0" -DLIBUSB_LIBRARIES="$prefix/lib/libusb-1.0.a" \
    >/dev/null
cmake --build "$prefix/build/libftdi1" -j"$jobs" --target ftdi1-static >/dev/null
cp "$prefix/build/libftdi1/src/libftdi1.a" "$prefix/lib/"
mkdir -p "$prefix/include/libftdi1"
cp "$src/libftdi1-$LIBFTDI_REF/src/ftdi.h" "$prefix/include/libftdi1/"

cmake -S "$monome" -B "$prefix/build/libmonome" "${toolchain[@]}" "${common[@]}" -DMONOME_LIBFTDI=ON >/dev/null
cmake --build "$prefix/build/libmonome" -j"$jobs" --target monome_static >/dev/null
cp "$prefix/build/libmonome/libmonome.a" "$prefix/lib/"
cp "$monome/public/monome.h" "$prefix/include/"
echo "$prefix/lib/libmonome.a"
