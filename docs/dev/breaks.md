# brks: _core's firmware on the SSP

Record of the 0.1.0 port (2026-10-19). User-facing behaviour is in [plugins/breaks/README.md](../../plugins/breaks/README.md).

## Source

[schollz/_core](https://github.com/schollz/_core) v8.1.0 (`e702abc`) builds the firmware of three modules from one tree. A variant is a `lib/cmake/*_compile_definitions.cmake` file. `brks` runs the variant `lib/core_engine/host.h` selects:

- the five-knob, four-button panel;

- smear, bass, tremble and Amiga sample-and-hold in `audio_callback.h`;

- the Mode-hold functions.

Of the other two, one has the same panel without these additions, and one has a 4x4 button grid.

_core's VCV Rack module builds the firmware through `lib/core_engine`:

- `host.h` replaces GPIO, the ADC, FatFS and timers.

- `prepare.py` runs in two stages. `--source-only` expands the firmware's headers into `firmware.c`. `--generate-only` uses clang's JSON AST to move the 355 mutable globals and persistent locals into `struct CoreEngine`.

- The result is one translation unit, `engine.c`, with the C API of `core_engine.h`.

Option rejected: porting it as `ttyp` ports teletype, with one dlopen'd firmware copy per instance. `core_engine` already makes instances independent, and _core maintains it.

## Build

`scripts/fetch_core.sh host|ssp`:

1. Fetches the tarball and keeps `lib/`, `main.c`, `rack/src` and the test fixtures.

2. Runs `prepare.py --source-only`.

3. Appends `#include "ssp_api.inc"` to `firmware.c`.

4. Runs `prepare.py --generate-only`, so `Source/ssp_api.inc` is rewritten with the firmware.

Notes:

- The unit contains the target's preprocessed libc, so there is one per target.

- `prepare.py` takes `--target` but no `--sysroot`; a two-line clang wrapper adds it.

- GCC cannot compile the unit. It rejects the `_Float32` typedefs in clang-preprocessed glibc.

- The plugin's CMake therefore compiles it with the host's clang (`NO_CMAKE_FIND_ROOT_PATH`: the sysroot ships an ARM clang).

- It is compiled with `-fvisibility=hidden`; `brks.so` exports only the SSP entry points.

## The ARM-mode miscompile

The first SSP build of the engine segfaulted in `core_engine_create`, in `WS2812_set_brightness`. It ran correctly at `-O1`, and at `-O2`/`-O3` with `-mthumb`. The x86 build ran at every level.

The line is `ws->brightness = ws2812_brightness_values[brightness * 160 / 1001];`, with `brightness` a `uint8_t` clamped to 100. The IR is correct: `udiv i16 %5, 1001`. In ARM mode, clang 18.1.3 and 20.1.2 (`--target=arm-linux-gnueabihf -mcpu=cortex-a17 -O2`) emit:

```text
add   r1, r1, r1, lsl #2      @ b*5
mul   r1, r1, r2              @ * 33521
ldrb  r1, [r2, r1, lsl #5]    @ tab + (b*5*33521 << 5): the >> 25 is missing
```

Thumb-2 emits `lsr.w r1, r1, #25` before the load. GCC 8.4 folds the table read. A 10-line reproducer is the function above with a 16-byte table. It was not reported upstream.

The plugin compiles the engine `-mthumb -O3`. Thumb-2 and ARM code interwork under the AAPCS.

`tests/engine_test golden` checks the result. It hashes the PCM of _core's Rack fixture over 3000 blocks; the x86 build gives `6ac7a5920f5cc629`. The SSP build matched it in four scenarios on 2026-10-19: default knobs, Break at 1, all 16 effects forced, and 4 interleaved instances.

## Measurements (SSP, 2026-10-19)

- `sizeof(CoreEngine)`: 48088 bytes. 177 KB are allocated at creation; the rest of the RAM is the resident bank.

- One 441-frame block, the raw engine at `-O3 -mthumb`: 255 us mean at default knobs, 403 us with all 16 effects.

- In the plugin's 128-sample block, playing the default banks: 1.8-2.5% mean, 362 us worst at FIFO priority. At normal priority, about 9 in 7500 blocks took 3.7 ms. That was preemption beside Synthor; the SSP gives the test shell one CPU.

## Controls without combinations

The hardware reaches Volume, Filter, Gating and Play with Tap held. Driving the virtual panel's Tap button also runs `TapTempo_tap`, and two Tap presses within 1 s set the tempo. A scratch test pressing Tap three times 0.3 s apart moved the tempo from 170 to 155.

`ssp_api.inc` applies those branches' state directly:

- Volume calls `break_set` with `gpio_btn_taptempo_val` set to 0 for the call.

- Filter and Gating repeat the firmware's 10-line branches in the panel code.

- Play repeats the Tap + Mult branch.

Option rejected: timed press-and-turn gestures on the virtual panel. They also tap the tempo, and they leave the knob where the gesture turned it.

## Mult and Bank

The firmware counts a Mult or Bank press as a tap only if it is released within 200 ms (`lib/ectocore.h:1625`, `:1710`). A longer Bank press is how the hardware selects a bank with Sample. A longer Mult press divides the clock after 1 s. Presses on the SSP's buttons mostly last longer than 200 ms. A host test with the default card switched the bank for presses of 150 ms or less, and not for 190 ms or more.

- Mult is the Clock Out encoder, which sets the division directly. In 4 sends a 20 ms tap on each rising edge.

- Bank is Left/Right and In 5. The tap steps only forward, from the bank playing, so presses before the loop's end do not add up.

The pending bank is a persistent local of `input_handling()`, so `ssp_api.inc` cannot set it. `fetch_core.sh` adds a prototype and one call to `ssp_input_handling()` at the top of that function's loop, beside `prepare.py`'s own `host_selection_changed` hook. It asserts both anchors once, so an upstream change fails the build. The call runs the tap's branch from the pending bank, in either direction.

Option rejected: driving the virtual Bank button with timed taps. They cannot step back, and repeated taps before the loop's end land on the same bank.

## Restore and the transport

A new `CoreEngine` reads `playback_stopped = 1` until a card plays. `core_engine_set_state` copies `stopped`, so a restore built on `core_engine_get_state` restored a stopped transport while audio played. The first Play then started instead of stopping. The plugin's restore sets `stopped` and `muted` to false; `testRestore` covers it.
