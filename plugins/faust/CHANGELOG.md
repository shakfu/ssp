# Changelog

faust follows [semantic versioning](https://semver.org). Until 1.0.0, a minor version may change parameters, so presets saved with an earlier version can restore differently.

The version is set in `CMakeLists.txt` (`project(FAUST VERSION ...)`) and shown at the top right of the screen.

## [0.2.0] - 2026-10-10

Programs run as machine code from libfaust's LLVM JIT, in place of its interpreter, which used ~15x the CPU of a compiled module on the SSP. LLVM is the SSP's own `libLLVM-9.so`, which the SSP's Mesa drivers already link; a static copy would add ~25 MB and could clash with it.

A program whose output goes NaN or infinite is silenced and its state cleared; the status panel counts the silenced blocks until the next program loads.

`examples/filter_cv.dsp` is `filter.dsp` with cutoff CV on input 3, the program the README's walkthrough builds.

libfaust 2.85.9 built the JIT before storing the requested target, so on the SSP it compiled for the host CPU, which LLVM 9 names `generic`: ARMv4 without an FPU. Its calls into libm used the soft-float convention, and `tan()` returned 0 or NaN, silencing filters. `build_deps.sh` patches libfaust, and `fstr` requests `cortex-a17`, which LLVM 9 cannot detect on the SSP. See [docs/dev/faust-jit-tan.md](../../docs/dev/faust-jit-tan.md).

`[cv:N]` on a control adds input N to it as CV, once per block: an octave per volt with `[scale:log]`, else a tenth of the range per volt. See [README.md](README.md).

A program that declares `[nvoices:N]` in its options plays N voices from the MIDI notes of the general panel's input, through Faust's `freq`, `gain` and `gate` controls; the other controls drive every voice. Voices are ungrouped (`mydsp_poly` with group off), because a grouped control writes the voices from a process-wide GUI list that two `fstr` instances would share. `examples/poly.dsp` is a voice. See [README.md](README.md#midi).

## [0.1.0] - 2026-10-08

First release, as the module `fstr`: Faust programs from the card, run by libfaust 2.85.9's interpreter. Eight inputs and outputs, and the program's first 16 controls as p1..p16. See [README.md](README.md).
