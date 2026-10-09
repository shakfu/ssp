# Faust on the SSP

How to turn Faust `.dsp` code into SSP modules. Decision of 2026-10-08: build route A (`chrs`) first, then route D (`fstr`). Both are built. On 2026-10-09 `fstr` moved from libfaust's interpreter to its LLVM JIT.

## Target

Synthor does not load VST3 bundles. CMake builds a JUCE `VST3` target, but Synthor `dlopen`s the `.so` inside it and calls the C entry points in `plugins/common/SSPApi.h`. So every route ends in `SSPApi.h` glue, or in gen-dsp's native Percussa format. Faust's `faust2vst` and `faust2juce` stop short of either.

## The sk-engines precedent

sk-engines uses cyfaust only as a compiler: `cyfaust compile <file>.dsp -b cpp` emits `class mydsp` with `init`, `compute` and `buildUserInterface`. Its wrapper code does the rest:

| sk-engines file | Lines | Role |
|-|-|-|
| `src/engine/faust_arch.h` | 59 | `UI`, `Meta` and `dsp` base types, so the kernel compiles without Faust's GPL-with-exception headers |
| `src/engine/faust/faust_capture.h` | 83 | captures slider zones by label |
| `src/engine/faust/faust_fx.h` | 224 | the Daisy `IEngine` wrapper: `ParamId`, `DeckRef`, SDRAM arena |
| `src/engine/faust/faust_chain.h` | 192 | series and dual-deck engines |
| `scripts/gen_faust_engine.py`, `engine_gen_common.py` | 511 | manifest (JSON knob-to-slider map) to wrapper and build wiring |

`faust -lang cpp` emits the same C++, so cyfaust is a convenience. It needs `PYTHONUTF8=1`: the Faust libraries' metadata contain non-ASCII author names.

## Routes

| | Route | How | Cost / risk |
|-|-|-|-|
| A | Compile ahead, wrap in `ssp::engine` | Kernel header from cyfaust; a generic engine maps sliders to parameters and CV inputs | One `.so` per patch, each with a 4-character name and uid ([engines.md](engines.md#naming)) |
| B | Faust front end in gen-dsp's `ssp` backend | Emit native or `--ssp-format juce` modules from a `.dsp` | Reuses packaging and naming that already work on the device. Couples gen-dsp, a gen~ tool, to Faust |
| C | `faust2juce` | Faust's JUCE architecture | Its generic editor is useless on the SSP; still needs `SSPApi.h` glue. Rejected |
| D | Runtime Faust module | libfaust (the interpreter backend, ~8 MB in cyfaust's build) in a `ScriptEngine`/`ScriptProcessor` plugin; `.dsp` files loaded from the card | No rebuild per patch, the csnd/chuk workflow. Needs libfaust cross-built. Interpreter overhead on the SSP's ARM is unmeasured |

## Plan

1. **A, as `chrs`.** sk-engines' `chorus.dsp`, compiled by `scripts/faust_kernel.sh`, hosted by `plugins/common/engine/FaustEngine.h` and `FaustProcessor.*`. sk-engines' arch shim is vendored as `FaustArch.h`.
2. **D, as `fstr`.** Before porting, benchmark the interpreter against compiled C++ on the host, then on the SSP. Only the zone capture carries over from A: libfaust builds the UI at runtime.

B stays open if one tool should emit both gen~ and Faust modules.

## Interpreter benchmark

`plugins/faust/bench/faust_bench.cpp` runs `chorus.dsp` through the compiled kernel and through libfaust's interpreter: 48 kHz, 128-sample blocks, 20 s of noise, `-O3`. Host x86-64, 3 runs, 2026-10-08:

| | Load, one core |
|-|-|
| compiled (`ChorusKernel.h`) | 0.11-0.12% |
| interpreter | 1.93-2.01% |
| ratio | 16.9-17.2x |
| `.dsp` to factory | 16-22 ms |

On the SSP (2026-10-09), `chorus.dsp` in `fstr` shows ~20% average, 23% peak; `chrs` shows ~1.3%. Both include `EngineProcessor` overhead, so the ratio is ~15x, close to the host's 17x.

Faster runtime backends:

- MIR JIT (`INTERP_MIR_BUILD`): MIR has no 32-bit ARM target.
- LLVM JIT: Faust 2.85.9 supports LLVM up to 21, and LLVM targets ARMv7. cyfaust 0.2.0 ships `cyfaust_llvm` wheels for x86-64 Linux and arm64 macOS only, so the SSP needs LLVM cross-built.

LLVM JIT on the host, through `cyfaust_llvm` 0.2.0 (`opt_level` -1, the maximum), same `chorus.dsp`, 2 runs, 2026-10-09:

| | block 128 | block 4096 |
|-|-|-|
| interpreter | 1.07-1.70% | 1.03-1.35% |
| LLVM JIT | 0.10-0.15% | 0.08% |
| ratio | 11-12x | 13-17x |
| factory build | interp 24-27 ms, LLVM 50-72 ms | |

The C++ benchmark, rerun the same hour, gave compiled 0.17-0.18% and interpreter 2.3-3.0%: the machine's load varies between runs, so compare ratios within a run.

`faust_bench.cpp` with the JIT added, host LLVM 18, 3 runs, 2026-10-09:

| | Load, one core |
|-|-|
| compiled (`ChorusKernel.h`, g++ `-O3`) | 0.113-0.116% |
| LLVM JIT | 0.069-0.070% |
| interpreter | 2.00-2.02% |
| factory build | interpreter 16-18 ms, LLVM 32-36 ms |

The JIT beats the compiled kernel here, likely because it targets this CPU's full instruction set (inference).

## LLVM on the SSP

The buildroot sysroot ships LLVM 9.0.1 for the SSP: static libraries, headers and `libLLVM-9.so`. The SSP rootfs has `libLLVM-9.so` with the same exported symbols; Mesa's display drivers link it. libfaust 2.85.9 builds against LLVM 9 unchanged.

`fstr` links the shared `libLLVM-9.so`: it adds ~4 MB, against ~25 MB for the static libraries. It also uses the copy Mesa may already have loaded, where a second, static copy could clash. Under `qemu-arm`, a JIT-compiled `chorus.dsp` matches the compiled kernel within 3e-5 (float rounding).

`build_deps.sh` builds libfaust with the LLVM and interpreter backends: against the sysroot's LLVM for the SSP, the system's (`llvm-config`) for the host. The interpreter remains for `faust_bench.cpp`. The Faust libraries go to `build/deps/<target>/share/faust`.

## Route D decisions

`plugins/faust` (`fstr`) is a `ScriptEngine`, like `csnd` and `chuk`: eight inputs and outputs, p1..p16, the program path in the preset.

- **Controls come from the compiler.** `ScriptEngine::declared()` returns the specs a compile produced; the `@pN` comments `csnd` and `chuk` use are not read. The first 16 controls map to p1..p16.
- **Defaults apply on Load only.** A program chosen with Load sets its controls to the program's defaults once it compiles. Presets keep their values. A new module starts with every control at 0, so the built-in is written to sound there.
- **No CV per control.** This differs from `chrs`: a program reads CV as audio inputs, as Csound and ChucK programs do.
- **Each instance holds its program's ranges.** The audio thread maps p1..p16 with the instance's own specs, so a swap never pairs a program with another's ranges for a block.
- **Non-finite output is cleared.** A NaN or infinite block is silenced and the program's state reset, so a recursive program recovers; the status panel counts these.
- **The JIT compiles for `cortex-a17` on ARM**, at LLVM's highest optimisation level (`-1`). LLVM 9 detects the SSP's CPU as `generic`, so the target is explicit; libfaust needs `scripts/patches/faust-2.85.9-jit-target.patch` to honour it.
- **Compiles are serialised.** libfaust keeps global compiler state; a static mutex guards factory creation and deletion across `fstr` instances.

## Route A decisions

- **No manifest.** Every kernel control becomes a parameter, named and ranged by the `.dsp`. sk-engines needed a manifest to map sliders onto 6 fixed panel knobs; the SSP has pages. A manifest returns if a patch needs fewer or renamed controls.
- **One CV input per control.** It adds to the parameter; 1.0 (5 V) spans the control's range, as in `gltc`. It is read once per block.
- **Buttons are controls.** `button` and `checkbox` become 0..1 parameters, so a CV gate can drive them.
- **The kernel is generated, then committed.** `make faust-kernels` reruns `scripts/faust_kernel.sh`; the build does not need cyfaust.

## Open questions

- `fstr`'s JIT matches compiled code on the SSP: `chorus.dsp` ~1.3% average, 3.3% peak, as `chrs`'s ~1.3% (2026-10-09). Before the target fix it cost ~3.9%, compiled for ARMv4 without an FPU. `chrs`'s peak is unrecorded. A Load's compile time is not noticeable.
- `tan()` in JIT code was wrong on the SSP: libfaust ignored the requested target, and LLVM 9 detects the SSP's CPU as `generic`. Patched; `filter.dsp` works on the SSP at ~0.9% DSP average (2026-10-09). See [faust-jit-tan.md](faust-jit-tan.md).
- `classInit` fills static tables shared by all instances of a kernel. A second instance's `prepare` rewrites them while the first plays. Harmless while the values are identical; it matters if a kernel's tables depend on the sample rate.
- `chorus.dsp` caps the delay at 2048 samples: 42 ms at 48 kHz. Its 25 ms maximum clips above 81.9 kHz.
