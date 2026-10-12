# Cleanup problems: plugin unload and reload

Status as of 2026-10-12. Remaining work; nothing here is fixed yet.

## Cause

Synthor unloads a plugin's `.so` (`dlclose`) when its last module is deleted, and loads it again for the next one. Each reload runs the library's process-wide setup again. The previous copy's process-wide memory and threads are never released, because the code that would release them is gone. Max and Pd keep externals loaded, so `chuck-max` and `pd-chuck` never see this.

Confirmed 2026-10-12: after adding and deleting one `chuk` in a blank patch, `chuk.so` was no longer in `/proc/$(pidof SYNTHOR)/maps`.

Two things kept plugins loaded after their last module was deleted (2026-10-12):

1. **`fstr.so` from Synthor's startup scan on (fixed).** The scan opens all 52 plugins on the card and closes them again. `fstr.so` is the first library to load `libLLVM-9.so`, which is `NODELETE`. glibc's trace (`LD_DEBUG=files`) showed libLLVM binding its own `std::` template symbols to the copies `fstr.so` exported (1646 symbols), which made `fstr.so` a "relocation dependency" of a library that never unloads. `plugins/faust/exports.map` now exports only the 8 entry points. Confirmed: after Synthor's startup, no plugin is mapped.
2. **Any plugin whose editor reaches JUCE's text shaper (open).** gdb on `__cxa_thread_atexit_impl` showed `juce::Unicode::performAnalysis` (`thread_local LruCache`, `juce_Unicode.cpp:84`) and `juce::BidiLine::computeVisualOrder` (`thread_local std::vector`, `juce_UnicodeBidi.cpp:60`) registering destructors on Synthor's main thread, from `Graphics::drawSingleLineText` in `ssp::EditorHost::drawBasicPanel`. glibc does not unload a library with pending `thread_local` destructors, and the main thread never exits. Seen in `fstr`, `chuk` and `csnd` after opening and cancelling the Load browser; adding and deleting `fstr` alone did not reach it.

Tools used: `scripts/ssp-memlog.sh`, `scripts/ssp-crash.gdb`, a gdb breakpoint on `__cxa_thread_atexit_impl`, and a loader that `dlopen`s a plugin, runs one instance through the SSP API (optionally loading a program through its state), deletes it and `dlclose`s it under `LD_DEBUG=files`.

## Measurements

A sampler on the SSP read Synthor's `VmRSS`, thread count and open files once a second (`/proc/<pid>/status`, `/proc/<pid>/fd`). The user added and deleted each module in a blank patch: `chuk`, `csnd`, `fstr`, `scsy`, in that assumed order. The 1 s sampling missed some short cycles. No crash occurred, and open files returned to 28 after every cycle.

| Cycles | Probably | Resident memory with no module loaded | Threads afterwards (baseline 8) |
|-|-|-|-|
| 1-3 | `chuk` | 210.8, 213.7, 216.6 MB: +2.9 MB per cycle | 8 |
| 4 | unknown, 1 extra thread while loaded | 216.6 to 229.9 MB once | 8 |
| 5-10 | `csnd` or `fstr` | 232.7, then flat at 233.65 MB | 8 |
| 11-14 | `fstr` or `csnd` | 252.29, 252.64, 252.99, 253.32 MB: +0.35 MB per cycle | 8 |
| 15-18 | `scsy` | 264.48, 264.54, 265.25, 265.89 MB: about +0.5 MB per cycle | 9, 10, 11, 12 |

The mapping of cycles to modules is an assumption. Confirm it before acting on the per-module numbers.

## Problems

1. **`scsy` leaks one thread per reload.** Fix in progress (2026-10-13): the last `ScWorld` to close calls `deinitialize_library()`, after `World_Cleanup(world, false)`; `scripts/patches/supercollider-3.14.1-unload.patch` makes that repeatable (World_New reloads when `gUnitDefLib` is null; the tables are freed). Host test counts threads from before the first engine and checks a reload makes sound. On the SSP (2026-10-13), about 16 add/load `midi_saw`/delete cycles: idle threads back to 7 and open files to 28 every time, `scsy.so` and all 196 UGen libraries unmapped.
   - One cycle crashed Synthor: the first with gdb detached, so there is no trace. 15 others did not, 10 of them with gdb attached in `auto-solib-add off` mode.
   - With gdb reading symbols, each add and delete took about 9 s (196 libraries). Without, loading them takes 0.10 s and unloading 0.01 s (loader on the SSP). `scripts/ssp-crash.gdb` now turns symbol reading off after attaching, and reads all symbols on a crash.
   - Idle memory grew steadily: 197.68 MB at the start, 212.88 MB after about 16 cycles, about +0.9 MB per cycle. Either per-load allocations some libraries never free, or glibc keeping freed pages.
   - libscsynth's "load UGens once" state lives in `scsy.so`, so it starts over on each reload. scsynth then runs every UGen library's `PluginLoad` again.
   - `PluginLoad(DiskIO)` creates and starts a new `DiskIOThread` each time (`DiskIO_UGens.cpp:633`). Only `PluginUnload` deletes it, and `ScWorld` calls `World_Cleanup(world_, false)`, which keeps the UGens loaded.
   - The thread runs in `DiskIO_UGens.so`, which stays mapped, so it waits harmlessly but is never freed.
   - The UGens also reload each time, about 0.4 s from the card.
2. **Memory grows per reload in `chuk`, `fstr` or `csnd`, and `scsy`.**
   - ChucK's type system, libfaust and LLVM globals, and scsynth's UGen tables are rebuilt in a fresh copy each time.
   - Which module grew 0.35 MB and which grew 0 is unconfirmed.
3. **Measured per load in a clean process (2026-10-13).** A loader on the SSP ran N cycles of dlopen, create, prepare, delete, dlclose, printing `VmRSS` after each:

   | Plugin | Per cycle |
   |-|-|
   | `chuk` | about 2.9 MB |
   | `scsy` | about 0.6 MB, with or without the 196 UGen libraries |
   | `fstr` | about 0.06 MB, after a one-time 30 MB (`libLLVM-9.so`, which stays) |
   | `csnd`, `chrs`, `sfct` | flat |

   Valgrind on the host (`--keep-debuginfo=yes`, since the libraries are unloaded):
   - `chuk`: each ChucK VM imports the built-in classes into its type system (`type_engine_import_class_begin`, `Chuck_DLL::query`, `init_class_*`), and deleting the VM does not free them: about 1.3 MB definitely lost per VM, plus blocks reachable only through them. A ChucK limitation, per VM, not only per reload.
   - `scsy`: 576 kB of the 601 kB still reachable at exit came from `SC_fftlib.cpp`'s static initialiser (FFT window and cosine tables), never freed; lost on every reload of `scsy.so`. Fixed: the unload patch frees them in a static destructor. Valgrind: still reachable 615 kB to 25 kB. SSP loader, 20 cycles: 5.2 MB after 20 (no UGens) and 5.7 MB (196 UGens), growth flattening (+0.1 to +0.15 MB over cycles 10-20), where it was 0.6 MB per cycle.
   - `sfct`: no leak (flat over 10 cycles on the SSP, 3,468 to 3,480 kB). Its crash in the loader was the loader's: `sfct` reads 18 input channels and the loader passed 16. `SSP_PluginInterface::process` does not check that the host passes as many channels as the plugin reads; Synthor evidently does.
4. **Not measured:** `rack`, `rdio`, `bard`, `edrm`, `gltc`, `strc`.

## Fixed already, same class

Threads that could outlive their `.so`:

- `BaseProcessor`'s MIDI thread (all 12 plugins): detached, now joined. gdb caught a crash at an unmapped address with no usable backtrace; the cause was inferred from the code.
- ChucK's `KBHit` thread: compiled out (`__DISABLE_KBHIT__`, `__DISABLE_HID__`, `__ALTER_HID__`). The last `chuk` also runs `ChucK::globalCleanup()`. Identified with gdb: the crashing thread's program counter was `kb_loop+60`.
- `rack`'s track threads: detached, now joined. Found by reading the code; no crash observed.

Each engine's tests check that no thread outlives the engine (`testNoThreadsLeft`).

## Options

Rejected (user, 2026-10-12): keeping our libraries loaded on purpose. Plugins should clean up so that they unload. Fixed so far: `chuk` (`KBHit` compiled out, last-instance `ChucK::globalCleanup()`), `fstr` (exports map; last-instance `deleteAllDSPFactories()`). Fixed: the JUCE `thread_local` caches, patched by `scripts/patches/juce-8.0.10-thread-local.patch` (a lock-guarded static cache; a per-call vector), applied to a copy of `juce/` in the build directory. On the SSP (2026-10-12), `csnd`, `chuk` and `fstr` each unmapped after add, open browser, cancel, delete, with no `__cxa_thread_atexit_impl` calls.

Open: one `fstr` cycle crashed Synthor (SIGSEGV on the main thread in `libLLVM-9.so`; the raw stack showed `MCJIT::generateCodeForModule` and `MCJIT::notifyObjectLoaded`). `fstr.so` was mapped at the time: it was its second copy in that process, after the startup scan's load and unload. The next identical cycle did not crash. A loader that loads and unloads `fstr.so` once, then compiles and runs in a second copy, did not reproduce it. Untested differences from that loader: Mesa sharing `libLLVM-9.so`, and gdb, which hooks LLVM's JIT debugger interface that `notifyObjectLoaded` calls. Which step of the cycle crashed was not recorded. Log: `/tmp/gdb-crash1.log` on the SSP until it reboots. The next 11 cycles in a fresh Synthor (add, wait 5 s, browser, cancel, delete, with gdb attached) did not crash: 1 crash in 12 cycles.

Memory over those cycles, idle between them: 217.26 to 219.20 MB, about +0.3 MB per cycle, with open files back to 28 each time. Either per-load state left in `libLLVM-9.so`, which never unloads, or freed memory glibc keeps in fragmented pages; `VmRSS` cannot tell them apart.

Earlier options, kept for the record:

1. **Keep our plugin libraries loaded.**
   - At load time, each plugin reopens itself: `dlopen(own path, RTLD_NOW | RTLD_NOLOAD | RTLD_NODELETE)`, with the path from `dladdr`. Synthor's `dlclose` then leaves it mapped, as Max and Pd do.
   - Fixes: one-time setup runs once per process. That stops the `scsy` thread leak and its 0.4 s reload, and protects against any future thread that outlives its `.so`.
   - Costs:
     - Each plugin type stays in memory after its last module is deleted: its code plus one-time globals.
     - Process-wide state persists until Synthor restarts, so a fault in it no longer clears with a reload.
     - On the device it hides threads that outlive their engine. The host checks (`testNoThreadsLeft`) still catch them.
   - Implementation: one helper in `plugins/common`, called from each plugin's entry point.
2. **Last-instance cleanup per engine.**
   - `scsy`: call `World_Cleanup(world, true)` when the last engine closes, which runs `PluginUnload` and joins DiskIO's thread.
   - Narrower: it does not address the growth in `chuk` and `fstr`, and each library with process-wide state needs its own handling.

## To verify a fix

1. Deploy, then start the sampler (`scripts/ssp-memlog.sh`) and attach gdb (`scripts/ssp-crash.gdb`). The usage lines at the top of each copy them to the SSP's `/tmp`, which a reboot clears.
2. Record the order of modules and the number of cycles.
3. With option 1, expect: idle threads back to baseline after every `scsy` cycle, and idle memory flat after each module's first cycle.
