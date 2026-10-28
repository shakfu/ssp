# prdt: a libpd module

Status, 2026-10-28: built (`plugins/prdt`), host-tested, and tested on the SSP: the engine on its CPU, the unload loop, and the module in Synthor. Started as a proposal on 2026-10-27; the [Decisions](#decisions) settle its open questions.

`prdt` / `PRDT` runs a Pure Data patch through [libpd](https://github.com/libpd/libpd): libpd 0.16.1 (`ba0dc63`), which pins Pure Data 0.56-5 (`f009fd8`), vendored in `external/libpd`. Paths below are relative to that tree. The user-facing behaviour is in [plugins/prdt/README.md](../../plugins/prdt/README.md).

## Decisions

| # | Question | Decision |
|-|-|-|
| 1 | Name | `prdt`, uid `PRDT` ([engines.md](engines.md#naming)) |
| 2 | Patch or folder | Load picks one `.pd`. Its folder resolves abstractions and `[declare -path]`, then `BOOT/prdt/lib` and `BOOT/prdt/extra` |
| 3 | Channel counts | Fixed at 8 in, 8 out. Synthor reads the bus layout when it scans plugins, so a patch cannot change the jacks |
| 4 | Controls | `@pN name ...` in a comment sends to `[r name]`; undeclared ones to `[r pN]` |
| 5 | MIDI | In, all channel messages. Out not yet |
| 6 | Externals | Compiled in only: Pd's `extra/` without `pd~`, and cyclone 0.9-5 (2026-10-28) |
| 7 | Display | Status and the last 4 console lines. No GUI |
| 8 | Instances | `PDINSTANCE` + `PDTHREADS`, one instance per load |
| 9 | Source | Vendored source copy, not a submodule |

## Fit with the engine layer

`PdEngine` is a `ScriptEngine` in a `ScriptProcessor`, like `CsoundEngine`. It reuses Load, `p1`..`p16`, `@pN` specs, CV per control, keep-previous-on-failure, the preset path and the `ReloadGate` swap. It has no `CodeKeys`: a `.pd` file is graph data, so the shared editor and Ctrl-E do not apply.

## Facts from the source

| Fact | Where | Consequence |
|-|-|-|
| With `PDINSTANCE` and `PDTHREADS`, `pd_this` is `__thread` | `pure-data/src/m_pd.h:1059-1080` | each thread selects its instance; every entry point calls `libpd_set_instance` first |
| Classes are process-wide, with one method list per instance; `pdinstance_new` copies every class's methods | `m_class.c:165-197, 506-512` | `extra/` setup runs once, in `initPd()`; later instances get the objects (tested) |
| Every libpd call takes `sys_lock`: the instance's mutex plus a read lock on one process-wide rwlock | `s_inter.c:1962-2045` | a write lock anywhere stalls every module's audio |
| The write lock (`pd_globallock`) is taken by instance create and free, by the external loader for an unknown object, and by `libpd_openfile` for the whole open | `m_class.c:174, 210, 963`, `z_libpd.c:144` | `prdt` opens with `sys_lock` only (`openText`); the loader takes the write lock itself when it needs it |
| `libpd_init` runs once per process; it ignores `SIGFPE` and sets `LC_NUMERIC` to `"C"` for the whole process | `z_libpd.c`, `libpd_init` | affects all of Synthor; ARMv7 VFP does not trap, so `SIGFPE` changes nothing there *(inference)* |
| Under `LIBPD_EXTRA`, `libpd_init` registers `pd~`, which forks a `pd` process | same | `plugins/prdt/pd/CMakeLists.txt` builds `extra/` itself, without `pd~` |
| The loader `dlopen`s `name.pd_linux` with `RTLD_NOW` from the search path | `s_loader.c:317` | `exports.map` hides Pd's symbols in `prdt.so`, so an external fails to link: "undefined symbol: gensym" (tested). Under `PDINSTANCE` an external must also be built with `PDINSTANCE` and `PDTHREADS`, and resolve `pd_this` from the host ([libpd#364](https://github.com/libpd/libpd/issues/364)). Compiled-in externals get both by linking `libpd_public_settings`, as `pd_extra` does |
| `sched_tick` increments the process-wide `sched_counter` without a lock | `m_sched.c:389` | a race between instances; only the GUI's meter polling reads it. Suppressed in `tests/tsan.supp` |
| Pd prints a failed object's text, then the error `... couldn't create` | `g_text.c:109-120` | the load error joins the two lines |
| Licence: BSD-3-Clause, libpd and Pd | `LICENSE.txt`, `pure-data/LICENSE.txt` | compatible with the repo's AGPL-3 |

## Threads

| Thread | Pd calls |
|-|-|
| Worker (`ScriptEngine::idle`) | new instance, open, close, free |
| Audio | MIDI drain, control sends, ticks |
| UI | none: `status()` drains a lock-free queue of printed lines |

- A load makes a new instance, opens the patch in it, and swaps it in through `ReloadGate`. A failed load frees it, and the running patch continues.
- The audio thread tries the locks (`sys_trylock`). If a write lock is held, it outputs one block of silence instead of waiting on the worker, which runs on core 0 at normal priority.
- The audio thread runs Pd ticks itself, one frame at a time, like `csnd`'s k-cycle. Output is one tick (64 frames) late at any block size. `libpd_process_raw` would need a 64-frame FIFO for other sizes, and takes the locks without trying.
- `readsf~` and `writesf~` start a thread per object (`d_soundfile.c:2161, 2758`). The worker opens the patch, so they start on core 0.

The ThreadSanitizer test runs two modules' audio threads while a third thread reloads both, 40 times each.

## Build

- `plugins/prdt/pd/CMakeLists.txt` adds `external/libpd/CMakeLists.txt` with `PD_MULTI` on, `PD_EXTRA` off, `PD_UTILS` on for the print concatenator, static and PIC. A second library holds `extra/`, and a third, `pd_cyclone`, cyclone: its object sources, less the excluded ones, and a setup that calls every lowercase setup function found in them (218), aliases of `zl` first. Pd's C builds with the plugin, so `scripts/build_deps.sh` has no libpd step.
- `exports.map` exports only the SSP entry points.
- `plugins/prdt/tests/CMakeLists.txt` builds the engine test from the same files. `test_prdt.py` runs it as release, ThreadSanitizer and AddressSanitizer builds in `build/prdt-host/`.
- `plugins/prdt/tests/run_on_ssp.sh` cross-builds the same test and runs it on the SSP over ssh, with the thread test pinned to the isolated cores and a DSP-time bench.
- Updating libpd: replace `external/libpd` from a new tag, keep only `libpd_wrapper/`, `pure-data/src`, `pure-data/extra`, `CMakeLists.txt` and the licences, update `VERSION`, and reapply the patches in `external/libpd/PATCHES.md`.

## Risks

1. **Externals.** Most published patches use ELSE or cyclone. cyclone is in (`external/pd-cyclone`, BSD-3-Clause); ELSE is not. Its review found cached symbols, shared scratch statics, a thread with no Pd instance, a use-after-free and leaks, all patched ([external/pd-cyclone/PATCHES.md](../../external/pd-cyclone/PATCHES.md)); a test creates and frees every object under AddressSanitizer. Each library also needs a review for static state, which two `dsp` threads would share. [plugdata](https://github.com/plugdata-team/plugdata) links both into a multi-instance libpd and is GPL-3. Its multi-instance patches are worth reading before choosing a library. Adding one follows [libpd#140](https://github.com/libpd/libpd/issues/140) and [ofxPd](https://github.com/danomatika/ofxPd#adding-pure-data-external-libraries-to-ofxpd): its sources go into a library beside `pd_extra`, and `initPd()` calls its setup (`zexy_setup()`, `cyclone_setup()`) once, with any defines its own build sets.
2. **Overlap.** Six live-coding modules exist (`csnd`, `chuk`, `fstr`, `scsy`, `sapf`, `ttyp`). Pd adds its existing patches and visual-patching users, not new DSP. That case depends on risk 1.
3. **Unload.** Synthor `dlclose`s a plugin after its last module ([cleanup-problems.md](cleanup-problems.md)). Pd has no teardown, so each unload left Pd's classes, main instance and `cos~` and FFT tables: about 210 kB on the SSP. The local patch `pd_freeprocess()` ([external/libpd/PATCHES.md](../../external/libpd/PATCHES.md)) frees them from a static destructor in `PdEngine.cpp`. Measured on the SSP, 2026-10-28, with a loader that cycles `dlopen`, create, load, process, delete and `dlclose`: RSS is flat after warm-up, as are 300 patch reloads in one instance. Left: 512 bytes of libpd's message buffer per unload, and upstream's 54 bytes per `inlet~` or `outlet~` of an abstraction per load (`tests/lsan.supp`).
4. **Load stalls.** An unknown object makes the loader probe the search path under the write lock. Every `prdt` module outputs silence for the length of the probe.

## Alternatives

- **Heavy, ahead of time** ([hvcc](https://github.com/Wasted-Audio/hvcc)). Compiles a patch to C, built as a normal module: what `chrs` is for Faust. No runtime, no shared globals, predictable CPU. Loses dynamic patching, most externals and loading on the device.
- **Ctrl-E as a message box.** Send a block as Pd messages (`; pd dsp 1`, `; pd-main.pd obj 10 10 osc~ 440`). This is dynamic patching, a niche form of live coding. It would reuse `runPart` unchanged.
- **Reload on file change.** Edit in desktop Pd, `scp` to the card; the module reloads when the file's mtime changes. Likely the best edit loop, and it would serve `csnd` and `chuk` too.

## Next

Open work is in [plugins/prdt/TODO.md](../../plugins/prdt/TODO.md).
