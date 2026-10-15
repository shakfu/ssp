#!/usr/bin/env bash
# Step 1 of docs/dev/sapf-plugin.md: build the harness for the PC and the SSP, evaluate
# sapf-examples.txt twice on the PC and once on the SSP, and compare (compare_render.py). Files go to
# /tmp/sapf on the SSP, which is RAM. Synthor must not be running: the harness measures render load.
#
# The PC build uses clang, as the SSP's does.
#
# usage: plugins/sapf/tests/run_on_ssp.sh   (SAPF_DIR, a sapf checkout, default the pinned one; SSP_HOST)

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
sapf="$(cd "${SAPF_DIR:-$("$root/scripts/fetch_sapf.sh")}" && pwd)"
host="${SSP_HOST:-root@192.168.1.6}"
here="$root/plugins/sapf/tests"
out="$root/build/sapf-engine-harness-out"
jobs="${JOBS:-$(nproc)}"

for t in host ssp; do
    b="$root/build/sapf-engine-harness-$t"
    tc=(-DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang)
    [ $t = ssp ] && tc=(-DCMAKE_TOOLCHAIN_FILE="$root/xcSSP.cmake")
    cmake -S "$here" -B "$b" "${tc[@]}" -DSAPF_DIR="$sapf" -DDEPS="$root/build/deps/$t" >/dev/null
    cmake --build "$b" -j"$jobs" >/dev/null
done

rm -rf "$out" && mkdir -p "$out/pc" "$out/pc2" "$out/ssp"
for r in pc pc2; do
    "$root/build/sapf-engine-harness-host/sapf_harness" "$sapf/sapf-prelude.txt" "$sapf/sapf-examples.txt" \
        "$out/$r" >"$out/$r.log" 2>"$out/$r.err"
done

ssh -o BatchMode=yes "$host" 'if pidof SYNTHOR >/dev/null; then echo "stop Synthor first" >&2; exit 1; fi
    rm -rf /tmp/sapf && mkdir -p /tmp/sapf/out'
scp -O -q -o BatchMode=yes "$root/build/sapf-engine-harness-ssp/sapf_harness" "$sapf/sapf-prelude.txt" \
    "$sapf/sapf-examples.txt" "$host":/tmp/sapf/
ssh -o BatchMode=yes "$host" 'cd /tmp/sapf && SAPF_BLOCK_SECONDS=300 SAPF_BLOCK_MB=512 ./sapf_harness sapf-prelude.txt \
    sapf-examples.txt /tmp/sapf/out >ssp.log 2>ssp.err; tar -cf - ssp.log ssp.err out; rm -rf /tmp/sapf' |
    tar -C "$out" -xf -
mv "$out/out"/* "$out/ssp/" && rmdir "$out/out"

tail -1 "$out/pc.err"
tail -1 "$out/ssp.err"
python3 "$here/compare_render.py" "$out/pc.log" "$out/pc2.log" "$out/ssp.log" "$out/pc" "$out/pc2" "$out/ssp"
