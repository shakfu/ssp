#!/usr/bin/env bash
# Cross-compile engine_test.cpp and the spike harness for the SSP and run them there over ssh, against
# the SSP build of the firmware. Files go to /tmp/ttyp on the SSP, which is RAM. Needs the SSP build
# (`make`, after `make deps` has fetched the teletype sources). With a ttyp running in Synthor, which
# holds the key port, the TCP test is skipped.
#
# usage: plugins/teletype/tests/run_on_ssp.sh   (SSP_HOST, default root@192.168.1.6)

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
host="${SSP_HOST:-root@192.168.1.6}"
br="${SSP_BUILDROOT:-${BUILDROOT:-$root/buildroot/arm-rockchip-linux-gnueabihf_sdk-buildroot}}"
sysroot="$br/arm-rockchip-linux-gnueabihf/sysroot"
cxxinc="$br/arm-rockchip-linux-gnueabihf/include/c++/8.4.0"
gcc="$br/lib/gcc/arm-rockchip-linux-gnueabihf/8.4.0"
tt="$root/build/deps/src/teletype"
fw="${BUILD_DIR:-$root/build.cmake.ssp}/plugins/teletype/firmware/ttyp_firmware.so"
src="$root/plugins/teletype/Source"
out="$root/build/ttyp-test-ssp"
mkdir -p "$out"
[ -f "$fw" ] || { echo "no $fw: build the plugins for the SSP first" >&2; exit 1; }

cpu=(--target=arm-linux-gnueabihf --sysroot="$sysroot" -mcpu=cortex-a17 -mfloat-abi=hard -mfpu=neon-vfpv4)
link=(-fuse-ld=lld -L"$sysroot/lib" -B"$sysroot/lib" -Wl,-rpath-link,"$sysroot/lib" -L"$gcc" -B"$gcc"
    -Wl,-rpath-link,"$gcc")
clang++ "${cpu[@]}" -O2 -std=c++17 -Wall -pthread -I"$cxxinc" -I"$cxxinc/arm-rockchip-linux-gnueabihf" -I"$src" \
    -I"$root/plugins/common" "$root/plugins/teletype/tests/engine_test.cpp" "$src/Firmware.cpp" "$src/Teletype.cpp" \
    "$root/plugins/common/keys/KeyInput.cpp" \
    -ldl "${link[@]}" -o "$out/engine_test"
clang "${cpu[@]}" -O2 -Wno-header-guard -D__AVR32_UC3B0512__ -I"$tt/monome-rack/firmware/mock_hardware/include" \
    -I"$root/plugins/teletype/firmware/include" -I"$tt/libavr32/src" "$root/plugins/teletype/spike/harness.c" -ldl "${link[@]}" \
    -o "$out/harness"

ssh "$host" 'rm -rf /tmp/ttyp && mkdir -p /tmp/ttyp'
scp -O -q "$out/engine_test" "$out/harness" "$fw" "$host:/tmp/ttyp/"
# the harness prints measurements; engine_test decides the exit status
ssh "$host" 'cd /tmp/ttyp; ./engine_test ./ttyp_firmware.so; rc=$?
    ./harness ./ttyp_firmware.so /tmp/ttyp | grep -v "^ *$"; rm -rf /tmp/ttyp; exit $rc'
