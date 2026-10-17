#!/usr/bin/env bash
# Step 2 of docs/dev/dsl-alda.md: build alda_test and psnd's suite runner for the SSP, and run them there:
# psnd's shared suite, the conformance run against Alda's output, the engine test, and the build time of
# each example score. Files go to /tmp/alda on the SSP, which is RAM, and are removed after. Synthor may
# run; the times are then taken beside it.
#
# usage: plugins/alda/tests/run_on_ssp.sh   (ALDA_DIR, default the pinned one; SSP_HOST)

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
alda="$(cd "${ALDA_DIR:-$("$root/scripts/fetch_alda.sh")}" && pwd)"
host="${SSP_HOST:-root@192.168.1.6}"
here="$root/plugins/alda/tests"
b="$root/build/alda-test-ssp"

cmake -S "$here" -B "$b" -DCMAKE_TOOLCHAIN_FILE="$root/xcSSP.cmake" -DALDA_DIR="$alda" >/dev/null
cmake --build "$b" -j"${JOBS:-$(nproc)}" >/dev/null

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/alda/examples" "$stage/alda/plugin-examples"
cp "$b/alda_test" "$b/alda_suite" "$stage/alda/"
cp -r "$alda/tests/shared_suite" "$alda/tests/alda_reference" "$stage/alda/"
cp "$alda"/examples/*.alda "$stage/alda/examples/"
cp "$root"/plugins/alda/examples/*.alda "$stage/alda/plugin-examples/"

ssh -o BatchMode=yes "$host" 'rm -rf /tmp/alda'
tar -C "$stage" -cf - alda | ssh -o BatchMode=yes "$host" 'tar -C /tmp -xf -'
ssh -o BatchMode=yes "$host" 'cd /tmp/alda; s=0
    echo "== shared suite"; ./alda_suite shared_suite | tail -2 || s=1
    echo "== conformance"; ./alda_suite examples alda_reference | tail -2 || s=1
    echo "== engine"; ./alda_test || s=1
    echo "== build times"; ./alda_test time examples/*.alda plugin-examples/*.alda | sort -n | tail -8 || s=1
    cd /; rm -rf /tmp/alda; exit $s'
