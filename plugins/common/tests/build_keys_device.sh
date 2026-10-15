#!/usr/bin/env bash
# Cross-compile keys_device.cpp for the SSP and copy it to /tmp/keys_device there (RAM). Run it over
# ssh while ttyp is loaded in Synthor; see docs/dev/sapf-sc-plugin.md, step 3.
#
# usage: plugins/common/tests/build_keys_device.sh   (SSP_HOST, default root@192.168.1.6)

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
host="${SSP_HOST:-root@192.168.1.6}"
br="${SSP_BUILDROOT:-${BUILDROOT:-$root/buildroot/arm-rockchip-linux-gnueabihf_sdk-buildroot}}"
sysroot="$br/arm-rockchip-linux-gnueabihf/sysroot"
cxxinc="$br/arm-rockchip-linux-gnueabihf/include/c++/8.4.0"
gcc="$br/lib/gcc/arm-rockchip-linux-gnueabihf/8.4.0"
out="$root/build/keys-device-ssp"
mkdir -p "$out"

clang++ --target=arm-linux-gnueabihf --sysroot="$sysroot" -mcpu=cortex-a17 -mfloat-abi=hard -mfpu=neon-vfpv4 \
    -O2 -std=c++17 -Wall -pthread -I"$cxxinc" -I"$cxxinc/arm-rockchip-linux-gnueabihf" -I"$root/plugins/common" \
    "$root/plugins/common/tests/keys_device.cpp" "$root/plugins/common/keys/KeyInput.cpp" \
    -fuse-ld=lld -L"$sysroot/lib" -B"$sysroot/lib" -Wl,-rpath-link,"$sysroot/lib" -L"$gcc" -B"$gcc" \
    -Wl,-rpath-link,"$gcc" -o "$out/keys_device"
scp -O -q -o BatchMode=yes "$out/keys_device" "$host":/tmp/keys_device
echo "on the SSP: /tmp/keys_device SHOWN HIDDEN"
