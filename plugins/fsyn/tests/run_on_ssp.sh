#!/usr/bin/env bash
# Build engine_test for the SSP and run it there: the tests with TimGM6mb, then the benchmark of each
# soundfont in FONTS (default: every .sf2 in BOOT/fsyn on the SSP), read from where it lies. The test goes
# to /tmp on the SSP, which is RAM, and is removed after. Synthor may run; the times are then taken beside it.
#
# usage: plugins/fsyn/tests/run_on_ssp.sh   (SSP_HOST; FONTS, paths on the SSP)

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
host="${SSP_HOST:-root@192.168.1.6}"
b="$root/build/fsyn-test-ssp"

cmake -S "$root/plugins/fsyn/tests" -B "$b" -DCMAKE_TOOLCHAIN_FILE="$root/xcSSP.cmake" \
    -DDEPS_DIR="$root/build/deps/ssp" >/dev/null
cmake --build "$b" -j"${JOBS:-$(nproc)}" >/dev/null

scp -O -q "$b/engine_test" "$host:/tmp/fsyn_test"
ssh -o BatchMode=yes "$host" "s=0; fonts=\"${FONTS:-}\"; [ -n \"\$fonts\" ] || fonts=\$(ls /media/BOOT/fsyn/*.sf2)
    echo '== tests'; /tmp/fsyn_test /media/BOOT/fsyn/TimGM6mb.sf2 || s=1
    echo '== bench'; /tmp/fsyn_test bench \$fonts || s=1
    rm -f /tmp/fsyn_test; exit \$s"
