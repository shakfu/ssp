# sapf: live-coding plugin (design)

Status: step 1 done, 2026-10-14 (see Step 1 results). Steps 2-4 not started.

`sapf` runs the [sapf-sc](https://github.com/shakfu/sapf-sc) interpreter inside an SSP plugin. You type sapf code on a USB keyboard, evaluate it, and hear it through the plugin's own scsynth World. scsy already plays sapf-sc's `>nrt` scores and `@buf` sidecars on the SSP ([scsy-sapf-changes.md](scsy-sapf-changes.md)); this plugin removes the laptop step.

## Requirements

- Standalone: a USB keyboard on the SSP. A laptop over ssh also works.
- Audio and CV in.
- A multi-line editor: sapf one-liners get long.
- `sapf-prelude.txt` loaded at start.

## Components

| Part | Source | Work |
|-|-|-|
| Interpreter | sapf-sc's `sapf_engine` (static) | fetched, pinned by commit; built for ARM without replxx |
| Server | `ScWorld` (`plugins/scsynth/Source`) and `libscsynth.a` | a raw-OSC api added; wrapped as sapf-sc's `ScsynthServer` |
| Keys | ttyp's `KeyInput` | moved to `plugins/common`; target becomes an interface; grab changes (see Keys) |
| Editor | new | text buffer, cursor, block evaluation, output pane, key repeat |
| Evaluation thread | new | runs code off the message and audio threads; interruptible |

No scsy instance is needed. `sapf.so` links its own copy of `ScWorld.cpp` and `libscsynth.a`, as `scsy.so` does.

## Evaluation model

Definitions persist across evaluations. The stack starts empty each time.

- After an evaluation, values left on the stack are printed as a warning, then cleared. sapf's REPL keeps them; here a leftover usually means a typo. A typo dropped `osc` from a `>nrt` test score without any message (sapf-sc docs/dev/ssp.md).
- The unit is a block: the lines around the cursor up to blank lines outside brackets and strings. Three of sapf's examples have blank lines inside a bracket; splitting on every blank line broke them into six syntax errors. A selection is not needed: sapf code has no statement separators across lines.
- Errors go to the output pane with the block's line number. The running sound keeps playing.

Ctrl-Enter evaluates the block. Ctrl-. interrupts a running evaluation, then runs sapf's `stop`.

## Server

`ScWorld` is shaped around scsy's model: one main def, mapped controls, a score feeder. sapf sends its own OSC (`/d_recv`, `/s_new`, `/n_query`, `/b_allocRead`, `/clearSched`, `/g_deepFree`) and waits for `/synced`. `ScWorld` needs:

- `send(packet)` made public. It is private now.
- `sendSync(packets, timeoutMs)`: sends, then `/sync`, and waits for `/synced` through the reply ring. `sendInBatches` already does this for buffer commands; sapf's `play` sends up to 30 wave-table buffers, so the batching applies.
- `now()`: the World's OSC time, which `oscNow_` already tracks for the score feeder.

`/g_deepFree 0` from sapf's `stop` frees synths in every group and keeps the groups, so `ScWorld`'s groups 900 and 901 survive it.

Audio in needs no work. sapf-sc's `soundin` compiles to `In.ar(NumOutputBuses + channel)` (`src/engine/SCUGens.cpp:946`), and `ScWorld` puts SSP inputs 1-8 on buses 8-15. `0 soundin` reads input 1, audio or CV (1.0 = 5 V). Inferred from the code; untested.

## Clock

sapf-sc times `play` and `ola` voices by the wall clock: `system_clock::now()` (`SCUGens.cpp:1016`), `sleep_until` in the voice scheduler (`Texture.cpp:123`), and NTP bundle times. `ScWorld` runs on OSC time advanced by samples. The two drift apart, and a stalled host stalls only one of them, so voices would start late, early, or be dropped as late.

Fix, in sapf-sc: `ScsynthServer` gets `virtual double now()`, the server's time in seconds since 1970. The default returns the wall clock, so the executable is unchanged. `play` and the scheduler read it. The scheduler sleeps on the wall clock for the difference between two `now()` values, which is close enough for a 0.2 s lookahead.

## Keys

`KeyInput` grabs the keyboard (`EVIOCGRAB`) when its first instance is created and holds it until the last is destroyed. A second plugin library cannot open the keyboard. With ttyp and sapf in one patch, whichever loaded first gets every key, whatever is on screen. TCP port 7770 binds the same way.

Fix: grab while shown. The editor's timer marks the target as shown, as ttyp's `onTeletypeTimer` does. The input thread grabs the keyboard only while a target was shown in the last second, and releases the grab otherwise. The plugin on screen holds the keyboard, and the other one rescans every second. This needs no state shared between libraries. ttyp changes with it and needs a device re-test.

- TCP: sapf listens on 7771, so a laptop can type into either plugin.
- Key repeat: the input thread drops evdev's autorepeat events (teletype's firmware repeats keys itself). The editor repeats held keys itself.
- Modifiers: the USB path delivers Ctrl, Shift, Alt and Meta as key down and up (HID 0xE0-0xE7). TCP carries a modifier byte only on taps. The editor tracks modifier state from both.

## Output

sapf-sc's `post` calls `vprintf` (`src/engine/Object.cpp:25`). sapf-sc gains a hook that sends output to a callback; the plugin appends it to the output pane, and the executable keeps stdout.

## Interrupt

sapf-sc has no interrupt: the REPL relies on Ctrl-C ending the process. Forcing an infinite list, or a long `>sf`, would block the evaluation thread for good. sapf-sc gains a flag that the VM checks in its call loop and in list forcing; a set flag throws an error that unwinds the evaluation. Where to check it is a sapf-sc design question, not settled here.

## Prelude

1104 lines, loaded by `SapfEngine::loadPrelude` on the evaluation thread when the plugin starts: 0.2 s on the SSP, 6.3 MB peak RSS. The editor accepts typing during the load and evaluates nothing until it ends.

## Screen

1600 x 480. With ttyp's 24 px monospace font, about 110 columns and 16 lines (estimated from the font size). Proposed layout: editor 11 lines, output 4 lines, status 1 line (CPU, voices, keyboard state, prelude state). Long lines wrap.

## Files

- Code: `/media/BOOT/sapf/*.sapf`. Ctrl-S saves the buffer, Ctrl-O opens a file.
- `>sd`, `>nrt` and `>sf` write to `/media/BOOT/sapf` (`SAPF_SYNTHDEFS`, `SAPF_RECORDINGS`).
- UGens: `/media/BOOT/sapf/ugens`, a copy of scsy's (see Risks).

## Risks

| Risk | Evidence | Mitigation |
|-|-|-|
| scsy and sapf share UGen libraries | Opening one path twice returns the same handle, and a UGen library's function table global then points at whichever server loaded it last. Unloading sapf while scsy runs could leave scsy's UGens calling unloaded code. Inferred; untested. | Separate UGen folder per plugin |
| One interpreter per process | sapf's VM is a global (`vm`) | Second instance shows "one sapf per patch" and stays silent |
| Runaway evaluations | Any infinite computation, such as `ord 1e12 N +/`, blocks the evaluation thread; one that allocates exhausts the SSP's 2 GB (no swap; Synthor at 636 MB virtual) | The interrupt also needs a memory cap |
| `log_nova` on ARM | Not exercised by step 1, which renders no audio | Step 2 renders |

## Step 1 results

`plugins/sapf/tests/run_on_ssp.sh` builds `harness.cpp` for the PC and the SSP, evaluates `sapf-examples.txt` (248 blocks, each `play` rewritten to a 2 s `>nrt`) on both, and compares with `compare.py`. `test_harness.py` covers the harness on the host.

- Match: logs equal, 155 defs and scores, no block skipped. 8 wave-table scores differ in 1 to 14 of about 320,000 floats, by one float32 step (relative 1e-7 to 5e-6): the SSP's libm rounds differently.
- Bug fixed in sapf-sc: `Array::setSize` and `addSize` took `size_t`. `Gen::produce` passes a negative delta; on 32-bit ARM it became 2^32 - k, and every list operation crashed. Both take `int64_t` now.
- Compiler, not ARM: built with gcc, six of the PC's scores order UGens differently from clang's, with different random draws. The likely cause, not yet located: sapf-sc builds some graphs in function argument evaluation order, which C++ leaves unspecified. The comparison builds both sides with clang. A sapf-sc issue to fix separately.
- Time: prelude 0.2 s on the SSP (0.04 s on the PC). Sound examples evaluate in milliseconds; the examples' number puzzles (Collatz, Kaprekar) run about 15 times slower than on the PC, up to 150 s.
- Memory: 6.3 MB peak after the prelude, 67 MB after all examples.
- Three examples (`native algorhythms`) grew past 2 GB on both machines: upstream `Z` calls an infinite stream finite, and `iseq` forced it to measure it. Fixed in sapf-sc (its docs/dev/upstream-sapf-bugs.md); all three now compile. The harness still evaluates each block first in a forked child with time and memory limits; the plugin cannot fork, so it needs the interrupt.

## Licence

GPL-3, as sapf and sapf-sc are. ttyp set the precedent in release 0.3.0.

## Plan

Each step ends with a check that must pass before the next.

1. **ARM build of the interpreter.** Done, except `scripts/fetch_sapf.sh`, which needs the sapf-sc changes committed to pin. Check passed; see Step 1 results.
2. **Server and clock.** sapf-sc: `ScsynthServer::now()`, the output hook. ssp: `ScWorld` raw-OSC api. A host test (pytest, as scsy's) drives a World from a loop, evaluates an `ola` program, and checks voice onsets against the score `>nrt` writes for the same program. Check: onsets within one 64-frame block.
3. **Keys.** `KeyInput` to `plugins/common`, grab while shown, port per plugin. Check: ttyp alone unchanged on device; ttyp and a stub second plugin swap the keyboard as the screen changes.
4. **Plugin.** Editor, output pane, evaluation thread, interrupt (sapf-sc), files. Check: device test script in `docs/dev/sapf-device-test.md`, covering typing, evaluation, errors, interrupt, audio in, unload and reload.

## Open questions

- Encoders: map the 16 parameters to `ctl` controls of the running synth, as scsy maps its sidecar's `@p` lines? sapf-sc's `>sd` `@p` work (its next-steps item 4) decides the format.
- MIDI: scsy's note handling, or none?
- The block boundary (blank lines outside brackets): does that fit how you write sapf?
