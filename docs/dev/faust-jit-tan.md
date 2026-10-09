# fstr: tan() was wrong in JIT code on the SSP

Found and fixed 2026-10-09. On the SSP with the fix, `filter.dsp` sounds (~0.9% DSP), and `chorus.dsp` costs ~1.3%, as the compiled `chrs`; before, ~3.9%. `fstr` 0.2.0 (libfaust 2.85.9 LLVM JIT, the SSP's LLVM 9.0.1).

## Cause

libfaust 2.85.9's `createDSPFactoryFromString` and `createDSPFactoryFromSignals` call `initJIT` before `setTarget` and `setOptlevel`. The JIT therefore compiles for the host CPU, and at the maximum optimisation level whatever the caller requests: the code container builds the factory with target `""` and level -1. On the SSP, LLVM 9 names the host CPU `generic`, so the JIT emits ARMv4 soft-float code. Its call to `tanf` passes x in `r0` and reads the result from `r0`; libm's hard-float `tanf` reads `s0` and returns in `s0`. `sinf` worked because LLVM lowers `llvm.sin` as a runtime call with the hard-float convention.

`qemu-arm` reported a real CPU (`cortex-a15`, `cortex-a57`), so the host fallback produced VFP code there and the bug never showed. With the patch, requesting `generic` under `qemu-arm` reproduces the SSP's output (1.4e-45 at x = 0.5 and 0.8); `cortex-a17` gives 0.546 and 1.030.

## Fix

- `scripts/patches/faust-2.85.9-jit-target.patch`: set the target and optimisation level before `initJIT`. `build_deps.sh` applies it once.
- `fstr` passes `<triple>:cortex-a17` on ARM, since LLVM 9 cannot name the SSP's CPU.

Upstream fixed the target in fc1031d214 (2026-07-20, in 2.88.0), not the level; issue draft: [issues/261008-jit-target.md](issues/261008-jit-target.md). The ignored level costs `fstr` nothing, since it requests the maximum.

## Symptom

A JIT-compiled `tan(x)` returns 0 for x below pi/4 (0.785) and NaN above it, on the SSP only. `fi.resonlp` and other filters use `tan`, so `filter.dsp` and `diag/resonlp.dsp` output NaN on every block; the NaN guard silences them.

The switch between x = 0.79 and 0.80 is pi/4, where glibc's `tanf` moves from its kernel to argument reduction. So the call into `tanf` goes wrong, not the code around it.

## Device readings (`diag/*.dsp` on the card, status line)

| Program | Result |
|-|-|
| `diag/tan.dsp` (`tan(x)`, x 0.5..1) | out 0, NaN count still; NaN rising from x = 0.80 |
| `diag/sin.dsp` | out 0.479, correct |
| `diag/resonlp.dsp` (fixed 1000 Hz) | out 0, NaN rising |
| `filter.dsp` | out 0, NaN rising |
| `chorus.dsp`, `osc.dsp` | correct |

- `fpscr 6100009a` on the audio thread: round-to-nearest, flush-to-zero, default-NaN off. Normal.
- `tanf(0.5)` from compiled C++ is 0.546 on both the audio and UI threads. libm's `tanf` works.
- Only libm (`/lib/libm-2.32.so`) exports `tanf` on the SSP rootfs.
- External calls in the IR: `filter.dsp` calls only `tanf`; `chorus.dsp` calls `sinf` and `floorf`; `osc.dsp` none.

## Ruled out

| Hypothesis | Test | Result |
|-|-|-|
| JIT codegen for ARM | `qemu-arm`, SSP sysroot libs and `libLLVM-9.so`, targets detected, `cortex-a17`, `cortex-a15`, `generic`; with and without flush-to-zero | correct everywhere, but invalid: the unpatched libfaust compiled every run for qemu's CPU |
| Another library's `tanf` wins the lookup | rootfs exports; `AddSymbol` binding of libm's functions | only libm exports it; binding changed nothing |
| Audio thread FPU mode breaks `tanf` | FPSCR and `tanf` from C++ on the audio thread | both normal |
| LLVM 9 targets `generic` (it does not recognise the RK3288) | forced `cortex-a17` target | no change: libfaust dropped the target (the cause) |

## How it was found

1. A wrapper registered as `tanf` (2026-10-09) received the right argument and returned the right result, yet the JIT code output 0: the result was lost on return.
2. `writeDSPFactoryToMachineFile` on the SSP gave an object with `ldr r0, [r0]; bl tanf; str r0` and `mov pc, lr`, and no CPU or FPU attributes. `qemu-arm`'s object had `vldr s0` and `Tag_CPU_name: cortex-a15`, though `cortex-a17` was requested.
3. The device's `libLLVM-9.so` has the same `.text` as the sysroot's, so the difference came from the host CPU query, and the request was being dropped.

The wrapper, the explicit libm binding and the object dump were removed after the diagnosis. `diag/tan.dsp`, `diag/sin.dsp` and `diag/resonlp.dsp` remain on the card in `BOOT/faust/diag`, not in the repo.

## Also open

- A Load left `filter.dsp`'s controls at 0 instead of the program's defaults.
