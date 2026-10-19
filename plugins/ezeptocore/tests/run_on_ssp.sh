#!/usr/bin/env bash
# Build engine_test for the SSP and run it there: the golden hash (a miscompile check), the tests with a card
# of _core's generated test samples, then the benchmark with CARD (a path on the SSP; default
# /media/BOOT/ezep/default, else the test card). Everything goes to /tmp on the SSP, which is RAM, and is
# removed after. Synthor may run; the times are then taken beside it.
#
# usage: plugins/ezeptocore/tests/run_on_ssp.sh   (SSP_HOST; CARD)

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
host="${SSP_HOST:-root@192.168.1.6}"
here="$root/plugins/ezeptocore/tests"
src="$root/plugins/ezeptocore/Source"
b="$root/build/ezep-test-ssp"
br="${SSP_BUILDROOT:-${BUILDROOT:-$root/buildroot/arm-rockchip-linux-gnueabihf_sdk-buildroot}}"
sysroot="$br/arm-rockchip-linux-gnueabihf/sysroot"
cpu=(-mcpu=cortex-a17 -mfloat-abi=hard -mfpu=neon-vfpv4 -O3)
core="$("$root/scripts/fetch_core.sh" ssp | tail -1)"
inc=(-I"$core/lib/core_engine" -I"$core/rack/src" -I"$core/test/rack" -I"$src" -I"$root/plugins/common")

mkdir -p "$b"
# as the plugin's CMakeLists.txt: Thumb-2, since clang miscompiles the engine in ARM mode
clang --target=arm-linux-gnueabihf --sysroot="$sysroot" "${cpu[@]}" -mthumb -fwrapv -ffp-contract=off -std=c11 -w \
    -c "$core/gen/ssp/engine.c" -o "$b/engine.o"
for c in "$here/golden.c" "$core/lib/pcg_basic.c" "$core/lib/core_engine/card_format.c"; do
    "$br/bin/arm-rockchip-linux-gnueabihf-gcc" "${cpu[@]}" -std=gnu11 -w "${inc[@]}" -c "$c" -o "$b/$(basename "$c" .c).o"
done
"$br/bin/arm-rockchip-linux-gnueabihf-g++" "${cpu[@]}" -std=c++17 -pthread -Wno-psabi "${inc[@]}" "$here/engine_test.cpp" \
    "$src/EzepEngine.cpp" "$core/rack/src/Storage.cpp" "$b"/{engine,golden,pcg_basic,card_format}.o \
    "$br/arm-rockchip-linux-gnueabihf/lib/libstdc++fs.a" -lm -o "$b/engine_test"

rm -rf "$b/card"
python3 -I -c "
import sys; sys.path.insert(0, '$core/test/rack'); import fixtures
for bank in (1, 2):
    for slot in range(3): fixtures.sample('$b/card', bank, slot, seconds=2)"

ssh -o BatchMode=yes "$host" 'rm -rf /tmp/ezep_test && mkdir /tmp/ezep_test'
tar cf - -C "$b" engine_test card | ssh -o BatchMode=yes "$host" 'tar xf - -C /tmp/ezep_test'
ssh -o BatchMode=yes "$host" "s=0; card=\"${CARD:-}\"
    [ -n \"\$card\" ] || { [ -d /media/BOOT/ezep/default ] && card=/media/BOOT/ezep/default || card=/tmp/ezep_test/card; }
    echo '== tests'; /tmp/ezep_test/engine_test /tmp/ezep_test/card || s=1
    echo '== bench'; /tmp/ezep_test/engine_test bench \$card || s=1
    rm -rf /tmp/ezep_test; exit \$s"
