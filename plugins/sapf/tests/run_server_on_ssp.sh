#!/usr/bin/env bash
# Step 2 of docs/dev/sapf-plugin.md on the SSP: sapf_server_test with the SSP's UGens
# (/media/BOOT/scsy/ugens) at 1 and 2 times real time, checked with onsets.py. Then renders log, log2
# and log10 of a signal in [-3, -1] on the host and the SSP and reports NaN (sapf's result) or finite
# (nova-simd's; a known difference, see sapf-sc docs/dev/ssp.md). The report does not fail the run.
# Files go to /tmp/sapf on the SSP, which is RAM. Needs `scripts/build_deps.sh ssp` and `host`.
#
# usage: plugins/sapf/tests/run_server_on_ssp.sh   (SAPF_SC_DIR, a sapf-sc checkout, default the pinned one; SSP_HOST)

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
sapf="$(cd "${SAPF_SC_DIR:-$("$root/scripts/fetch_sapf.sh")}" && pwd)"
host="${SSP_HOST:-root@192.168.1.6}"
tests="$root/plugins/sapf/tests"
b="$root/build/sapf-server-test-ssp"
out="$root/build/sapf-server-out"
program='\[0 0 impulse 64 N] .1 .3 randz 1 1 ola'
seconds=3

cmake -S "$tests" -B "$b" -DCMAKE_TOOLCHAIN_FILE="$root/xcSSP.cmake" -DSAPF_SC_DIR="$sapf" \
    -DSCSYNTH_DEPS="$root/build/deps/ssp" >/dev/null
cmake --build "$b" -j"${JOBS:-$(nproc)}" --target sapf_server_test >/dev/null

ssh -o BatchMode=yes "$host" 'rm -rf /tmp/sapf && mkdir -p /tmp/sapf'
scp -O -q -o BatchMode=yes "$b/sapf_server_test" "$sapf/sapf-prelude.txt" "$host":/tmp/sapf/
rm -rf "$out" && mkdir -p "$out"
status=0
for speed in 1 2; do
    mkdir -p "$out/$speed"
    ssh -o BatchMode=yes "$host" "cd /tmp/sapf && mkdir -p $speed && ./sapf_server_test sapf-prelude.txt \
        /media/BOOT/scsy/ugens /tmp/sapf/$speed '$program' $seconds $speed >$speed/stdout 2>$speed/stderr; \
        echo exit \$? >>$speed/stderr; tar -C $speed -cf - ."  | tar -C "$out/$speed" -xf -
    echo "speed $speed: $(tail -1 "$out/$speed/stderr")"
    python3 "$tests/onsets.py" "$out/$speed/stdout" "$out/$speed/sched.osc" $seconds || status=1
done

# log of a negative input; "stats NAN INF FINITE MIN MAX"
host_test="$root/build/sapf-server-test/sapf_server_test"
cmake -S "$tests" -B "$(dirname "$host_test")" -DCMAKE_CXX_COMPILER=clang++ -DSAPF_SC_DIR="$sapf" \
    -DSCSYNTH_DEPS="$root/build/deps/host" >/dev/null
cmake --build "$(dirname "$host_test")" -j"${JOBS:-$(nproc)}" --target sapf_server_test >/dev/null
mkdir -p "$out/log" "$out/ugens-host"
ln -sf "$root"/build/deps/host/scsynth/plugins/*.so "$out/ugens-host/"
verdict() { awk '/^stats/ { if ($2 > 0) print "NaN"; else printf "finite, %s to %s\n", $5, $6 }'; }
for op in log log2 log10; do
    program="440 0 sinosc 2 - $op"
    pc=$("$host_test" "$sapf/sapf-prelude.txt" "$out/ugens-host" "$out/log" "$program" 0.5 1 2>/dev/null | verdict)
    ssp=$(ssh -o BatchMode=yes "$host" "cd /tmp/sapf && mkdir -p log && ./sapf_server_test sapf-prelude.txt \
        /media/BOOT/scsy/ugens /tmp/sapf/log '$program' 0.5 1 2>/dev/null" | verdict)
    echo "$op of [-3, -1]: host $pc; SSP $ssp"
done

ssh -o BatchMode=yes "$host" 'rm -rf /tmp/sapf'
exit $status
