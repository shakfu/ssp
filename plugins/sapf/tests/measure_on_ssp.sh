#!/usr/bin/env bash
# Step 2 of docs/dev/sapf-plugin.md: render sapf-examples.txt on the SSP with each sapf block size
# given, and summarize render cost per 128-frame host block (loads.py). Synthor must not be running.
# About 15 minutes per block size.
#
# usage: plugins/sapf/tests/measure_on_ssp.sh [BLOCK_FRAMES...]   (default 512 128; SAPF_DIR, default the pinned fork; SSP_HOST)

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
sapf="$(cd "${SAPF_DIR:-$("$root/scripts/fetch_sapf.sh")}" && pwd)"
host="${SSP_HOST:-root@192.168.1.6}"
here="$root/plugins/sapf/tests"
out="$root/build/sapf-engine-measure"
sizes=("$@")
[ $# -eq 0 ] && sizes=(512 128)

b="$root/build/sapf-engine-harness-ssp"
cmake -S "$here" -B "$b" -DCMAKE_TOOLCHAIN_FILE="$root/xcSSP.cmake" -DSAPF_DIR="$sapf" -DDEPS="$root/build/deps/ssp" >/dev/null
cmake --build "$b" -j"${JOBS:-$(nproc)}" >/dev/null

mkdir -p "$out"
ssh -o BatchMode=yes "$host" 'if pidof SYNTHOR >/dev/null; then echo "stop Synthor first" >&2; exit 1; fi
    rm -rf /tmp/sapf && mkdir -p /tmp/sapf'
scp -O -q -o BatchMode=yes "$b/sapf_harness" "$sapf/sapf-prelude.txt" "$sapf/sapf-examples.txt" "$host":/tmp/sapf/
for s in "${sizes[@]}"; do
    ssh -o BatchMode=yes "$host" "cd /tmp/sapf && rm -rf out && mkdir out && SAPF_BLOCK_FRAMES=$s SAPF_BLOCK_SECONDS=300 \
        SAPF_BLOCK_MB=512 ./sapf_harness sapf-prelude.txt sapf-examples.txt out >/dev/null 2>err; cat err" >"$out/ssp-$s.err"
done
ssh -o BatchMode=yes "$host" 'rm -rf /tmp/sapf'

for s in "${sizes[@]}"; do
    echo "== sapf block $s"
    python3 "$here/loads.py" "$out/ssp-$s.err"
done
