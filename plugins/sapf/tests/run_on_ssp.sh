#!/usr/bin/env bash
# Step 1 of docs/dev/sapf-plugin.md: build the harness for the host and the SSP, evaluate the
# rewritten sapf-examples.txt on both, and compare the logs and the files written. Files go to
# /tmp/sapf on the SSP, which is RAM. About 13 minutes: the SSP runs the examples' number puzzles
# about 15 times slower than a desktop.
#
# The host build uses clang, as the SSP's does: sapf-sc builds some graphs in an order that depends on
# the compiler (probably function argument evaluation order), so gcc's defs differ in structure.
#
# usage: plugins/sapf/tests/run_on_ssp.sh   (SAPF_SC_DIR, default ../sapf-sc; SSP_HOST)

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
sapf="$(cd "${SAPF_SC_DIR:-$root/../sapf-sc}" && pwd)"
host="${SSP_HOST:-root@192.168.1.6}"
tests="$root/plugins/sapf/tests"
out="$root/build/sapf-harness-out"
jobs="${JOBS:-$(nproc)}"

for t in host ssp; do
    b="$root/build/sapf-harness-$t"
    tc=(-DCMAKE_CXX_COMPILER=clang++)
    [ $t = ssp ] && tc=(-DCMAKE_TOOLCHAIN_FILE="$root/xcSSP.cmake")
    cmake -S "$tests" -B "$b" "${tc[@]}" -DSAPF_SC_DIR="$sapf" >/dev/null
    cmake --build "$b" -j"$jobs" >/dev/null
done

rm -rf "$out" && mkdir -p "$out/host" "$out/ssp"
python3 "$tests/examples.py" "$sapf/sapf-examples.txt" "$out/examples.sapf"

"$root/build/sapf-harness-host/sapf_harness" "$sapf/sapf-prelude.txt" "$out/examples.sapf" "$out/host" \
    >"$out/host.log" 2>"$out/host.err"

ssh -o BatchMode=yes "$host" 'rm -rf /tmp/sapf && mkdir -p /tmp/sapf/out'
scp -O -q -o BatchMode=yes "$root/build/sapf-harness-ssp/sapf_harness" "$sapf/sapf-prelude.txt" \
    "$out/examples.sapf" "$host":/tmp/sapf/
ssh -o BatchMode=yes "$host" 'cd /tmp/sapf && SAPF_BLOCK_SECONDS=300 SAPF_BLOCK_MB=512 ./sapf_harness sapf-prelude.txt examples.sapf /tmp/sapf/out \
    >ssp.log 2>ssp.err; tar -cf - ssp.log ssp.err out' | tar -C "$out" -xf -
mv "$out/out"/* "$out/ssp/" && rmdir "$out/out"

tail -1 "$out/host.err"
tail -1 "$out/ssp.err"
python3 "$tests/compare.py" "$out/host.log" "$out/ssp.log" "$out/host" "$out/ssp"
