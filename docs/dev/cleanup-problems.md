# Cleanup problems: plugin unload and reload

Status as of 2026-10-12. Remaining work; nothing here is fixed yet.

## Cause

Synthor unloads a plugin's `.so` (`dlclose`) when its last module is deleted, and loads it again for the next one. Each reload runs the library's process-wide setup again. The previous copy's process-wide memory and threads are never released, because the code that would release them is gone. Max and Pd keep externals loaded, so `chuck-max` and `pd-chuck` never see this.

Evidence that Synthor unloads: threads left running after a module's deletion crashed with the program counter on an unmapped address (gdb on the SSP, 2026-10-11).

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

1. **`scsy` leaks one thread per reload.**
   - libscsynth's "load UGens once" state lives in `scsy.so`, so it starts over on each reload. scsynth then runs every UGen library's `PluginLoad` again.
   - `PluginLoad(DiskIO)` creates and starts a new `DiskIOThread` each time (`DiskIO_UGens.cpp:633`). Only `PluginUnload` deletes it, and `ScWorld` calls `World_Cleanup(world_, false)`, which keeps the UGens loaded.
   - The thread runs in `DiskIO_UGens.so`, which stays mapped, so it waits harmlessly but is never freed.
   - The UGens also reload each time, about 0.4 s from the card.
2. **Memory grows per reload in `chuk`, `fstr` or `csnd`, and `scsy`.**
   - ChucK's type system, libfaust and LLVM globals, and scsynth's UGen tables are rebuilt in a fresh copy each time.
   - Which module grew 0.35 MB and which grew 0 is unconfirmed.
3. **Not measured:** `sfct`, `rack`, `rdio`, `bard`, `edrm`, `gltc`, `strc`, `chrs`.

## Fixed already, same class

Threads outliving their `.so`, each of which crashed Synthor:

- `BaseProcessor`'s MIDI thread (all 12 plugins): detached, now joined.
- `rack`'s track threads: detached, now joined.
- ChucK's `KBHit` thread: compiled out (`__DISABLE_KBHIT__`, `__DISABLE_HID__`, `__ALTER_HID__`). The last `chuk` also runs `ChucK::globalCleanup()`.

Each engine's tests check that no thread outlives the engine (`testNoThreadsLeft`).

## Options

1. **Keep our plugin libraries loaded (recommended).**
   - At load time, each plugin reopens itself: `dlopen(own path, RTLD_NOW | RTLD_NOLOAD | RTLD_NODELETE)`, with the path from `dladdr`. Synthor's `dlclose` then leaves it mapped, as Max and Pd do.
   - Fixes: one-time setup runs once per process. That stops the `scsy` thread leak and its 0.4 s reload, and protects against any future thread that outlives its `.so`.
   - Costs: each plugin type stays in memory after its last module is deleted: its code plus one-time globals.
   - Implementation: one helper in `plugins/common`, called from each plugin's entry point.
2. **Last-instance cleanup per engine.**
   - `scsy`: call `World_Cleanup(world, true)` when the last engine closes, which runs `PluginUnload` and joins DiskIO's thread.
   - Narrower: it does not address the growth in `chuk` and `fstr`, and each library with process-wide state needs its own handling.

## To verify a fix

1. Deploy, start the sampler (`/tmp/memlog.sh` writes `uptime VmRSS Threads fds`), and attach gdb with `/tmp/crash3.gdb`. Both are in `/tmp`, so recreate them after a reboot. The commands are in the 2026-10-11 session; gdb cannot unwind past libc on the SSP, so the script dumps every thread's raw stack as symbols.
2. Record the order of modules and the number of cycles.
3. With option 1, expect: idle threads back to baseline after every `scsy` cycle, and idle memory flat after each module's first cycle.
