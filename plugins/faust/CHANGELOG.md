# Changelog

faust follows [semantic versioning](https://semver.org). Until 1.0.0, a minor version may change parameters, so presets saved with an earlier version can restore differently.

The version is set in `CMakeLists.txt` (`project(FAUST VERSION ...)`) and shown at the top right of the screen.

## 0.2.0 - 2026-10-09

Programs run as machine code from libfaust's LLVM JIT, in place of its interpreter, which used ~15x the CPU of a compiled module on the SSP. LLVM is the SSP's own `libLLVM-9.so`, which the SSP's Mesa drivers already link; a static copy would add ~25 MB and could clash with it.

A program whose output goes NaN or infinite is silenced and its state cleared. The status panel shows the last block's output peak, the count of those resets, and the CPU the JIT compiles for.

libfaust 2.85.9 built the JIT before storing the requested target, so on the SSP it compiled for the host CPU, which LLVM 9 names `generic`: ARMv4 without an FPU. Its calls into libm used the soft-float convention, and `tan()` returned 0 or NaN, silencing filters. `build_deps.sh` patches libfaust, and `fstr` requests `cortex-a17`, which LLVM 9 cannot detect on the SSP. See [docs/dev/faust-jit-tan.md](../../docs/dev/faust-jit-tan.md).

## 0.1.0 - 2026-10-08

First release, as the module `fstr`: Faust programs from the card, run by libfaust 2.85.9's interpreter. Eight inputs and outputs, and the program's first 16 controls as p1..p16. See [README.md](README.md).
