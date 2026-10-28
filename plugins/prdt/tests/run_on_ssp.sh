#!/usr/bin/env bash
# Cross-compile prdt_test (tests/CMakeLists.txt, with xcSSP.cmake) and run it on the SSP over ssh: the engine
# and example tests, the two-module thread test with the audio threads pinned to the isolated cores
# (PRDT_PIN) 20 times, and the DSP time per block. Files go to /tmp/prdt on the SSP, which is RAM.
#
# usage: plugins/prdt/tests/run_on_ssp.sh   (SSP_HOST, default root@192.168.1.6)

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
host="${SSP_HOST:-root@192.168.1.6}"
out="$root/build/prdt-test-ssp"

cmake -S "$root/plugins/prdt/tests" -B "$out" -DCMAKE_TOOLCHAIN_FILE="$root/xcSSP.cmake" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$out" -j"$(nproc)" >/dev/null

ssh -o BatchMode=yes "$host" 'rm -rf /tmp/prdt && mkdir -p /tmp/prdt/examples /tmp/prdt/extra /tmp/prdt/scratch'
scp -O -q -o BatchMode=yes "$out/prdt_test" "$host":/tmp/prdt/
scp -O -q -o BatchMode=yes "$root"/plugins/prdt/examples/*.pd "$host":/tmp/prdt/examples/
scp -O -q -o BatchMode=yes "$root"/external/libpd/pure-data/extra/*.pd "$host":/tmp/prdt/extra/

ssh -o BatchMode=yes "$host" 'set -e; cd /tmp/prdt
export PRDT_EXAMPLES=/tmp/prdt/examples PRDT_EXTRA=/tmp/prdt/extra
./prdt_test /tmp/prdt/scratch
for i in $(seq 1 20); do rm -rf scratch/*; PRDT_PIN=1 ./prdt_test /tmp/prdt/scratch threads; done
PRDT_PIN=1 ./prdt_test /tmp/prdt/scratch bench
echo "all passed"'
