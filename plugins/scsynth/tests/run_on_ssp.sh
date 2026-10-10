#!/usr/bin/env bash
# Cross-compile scsynth_test.cpp and scsy_test.cpp for the SSP and run them there over ssh: the World
# and sc3-plugins tests, the two-World test with the audio threads pinned to the isolated cores
# (SCSY_PIN) 20 times, and the module engine's test (controls, CV, MIDI). Files go to /tmp/scsy on
# the SSP, which is RAM. Needs `scripts/build_deps.sh ssp` first.
#
# usage: plugins/scsynth/tests/run_on_ssp.sh   (SSP_HOST, default root@192.168.1.6)

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
host="${SSP_HOST:-root@192.168.1.6}"
br="${SSP_BUILDROOT:-${BUILDROOT:-$root/buildroot/arm-rockchip-linux-gnueabihf_sdk-buildroot}}"
sysroot="$br/arm-rockchip-linux-gnueabihf/sysroot"
cxxinc="$br/arm-rockchip-linux-gnueabihf/include/c++/8.4.0"
gcc="$br/lib/gcc/arm-rockchip-linux-gnueabihf/8.4.0"
deps="$root/build/deps/ssp"
sc="$deps/include/supercollider"
out="$root/build/scsynth-test-ssp"
mkdir -p "$out"

inc=()
for i in include/common common include/server include/plugin_interface server/scsynth \
    external_libraries/boost external_libraries/boost_sync/include external_libraries/TLSF-2.4.6/src; do
    inc+=("-I$sc/$i")
done
# gcc 8 keeps std::filesystem in libstdc++fs; glibc 2.32 keeps shm_open in librt
build() {  # out test.cpp sources...
    local exe="$1" test="$2"
    shift 2
    clang++ --target=arm-linux-gnueabihf --sysroot="$sysroot" -mcpu=cortex-a17 -mfloat-abi=hard \
        -mfpu=neon-vfpv4 -O2 -std=c++17 -Wall -pthread -I"$cxxinc" -I"$cxxinc/arm-rockchip-linux-gnueabihf" \
        -DSC_AUDIO_API=SC_AUDIO_API_SSP -DSC_AUDIO_API_SSP=100 -DSC_MEMORY_ALIGNMENT=32 \
        -I"$root/plugins/scsynth/Source" -I"$root/plugins/common" "${inc[@]}" -I"$deps/include" \
        "$root/plugins/scsynth/tests/$test" "$@" "$deps/lib/libscsynth.a" "$deps/lib/libsndfile.a" \
        -L"$br/arm-rockchip-linux-gnueabihf/lib" -lstdc++fs -lrt -ldl -lm \
        -fuse-ld=lld -L"$sysroot/lib" -B"$sysroot/lib" -Wl,-rpath-link,"$sysroot/lib" \
        -L"$gcc" -B"$gcc" -Wl,-rpath-link,"$gcc" -o "$out/$exe"
}
src="$root/plugins/scsynth/Source"
build scsynth_test scsynth_test.cpp "$src/ScWorld.cpp" "$src/ScsyDef.cpp"
build scsy_test scsy_test.cpp "$src/ScsynthEngine.cpp" "$src/ScWorld.cpp" "$src/ScsyDef.cpp" \
    "$root/plugins/common/engine/ScriptEngine.cpp"

ssh -o BatchMode=yes "$host" 'rm -rf /tmp/scsy && mkdir -p /tmp/scsy/core'
tar -C "$deps/scsynth" -cf - plugins | ssh -o BatchMode=yes "$host" 'tar -C /tmp/scsy -xf -'
tar -C "$root/plugins/scsynth/tests" -cf - defs | ssh -o BatchMode=yes "$host" 'tar -C /tmp/scsy -xf -'
scp -O -q -o BatchMode=yes "$out/scsynth_test" "$out/scsy_test" "$host":/tmp/scsy/

ssh -o BatchMode=yes "$host" 'set -e; cd /tmp/scsy
for f in plugins/*.so; do ln -s /tmp/scsy/$f core/; done
./scsynth_test engine /tmp/scsy/core defs
./scsynth_test sc3 /tmp/scsy/plugins defs
for i in $(seq 1 20); do SCSY_PIN=1 ./scsynth_test threads /tmp/scsy/core defs 2>/dev/null; done
mkdir -p scratch && ./scsy_test /tmp/scsy/core defs /tmp/scsy/scratch
echo "all passed"'
