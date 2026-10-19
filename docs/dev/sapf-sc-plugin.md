# sapf-sc plugin (superseded)

Superseded 2026-10-15 by [sapf-plugin.md](sapf-plugin.md): sapf-sc differs too much from sapf, and becomes `stac`. The plugin code is in commit 77050c7.

Status: steps 1-3 done, 2026-10-14 (see their results). Step 4 built and tested on the host; device test pending.

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

Fix (`plugins/common/keys/KeyInput`): the grab follows the screen. The editor's timer marks its library as on screen through `setTarget`, as ttyp's `onTeletypeTimer` does; Synthor stops that timer when another module fills the screen (seen on the SSP). A library wants the keyboard unless another library is on screen and it is not (`setTarget` within 0.5 s). The kernel gives the grab to one library at a time, so with nothing on screen the holder keeps it, and a holder that goes (unloaded, or a test process that exits) is replaced within a second; the console and SysRq never see keys. Libraries share "the last one shown" through `/tmp/ssp-keys`: one 64-bit word, port and monotonic time, that every library (and process) maps. Two libraries on screen at once, as mini editors may be, leave the keyboard with its holder. A switch takes up to 0.8 s: the hold, the input thread's 100 ms poll and its 200 ms rescan. `test_keys.py` checks the hand-off with two copies of the code loaded as separate libraries.

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

## Step 1 results

`plugins/sapf/tests/run_on_ssp.sh` builds `harness.cpp` for the PC and the SSP, evaluates `sapf-examples.txt` (248 blocks, each `play` rewritten to a 2 s `>nrt`) on both, and compares with `compare.py`. `test_harness.py` covers the harness on the host.

- Match: logs equal, 155 defs and scores, no block skipped. 8 wave-table scores differ in 1 to 14 of about 320,000 floats, by one float32 step (relative 1e-7 to 5e-6): the SSP's libm rounds differently.

- Bug fixed in sapf-sc: `Array::setSize` and `addSize` took `size_t`. `Gen::produce` passes a negative delta; on 32-bit ARM it became 2^32 - k, and every list operation crashed. Both take `int64_t` now.

- Compiler, not ARM: built with gcc, six of the PC's scores order UGens differently from clang's, with different random draws. The likely cause, not yet located: sapf-sc builds some graphs in function argument evaluation order, which C++ leaves unspecified. The comparison builds both sides with clang. A sapf-sc issue to fix separately.

- Time: prelude 0.2 s on the SSP (0.04 s on the PC). Sound examples evaluate in milliseconds; the examples' number puzzles (Collatz, Kaprekar) run about 15 times slower than on the PC, up to 150 s.

- Memory: 6.3 MB peak after the prelude, 67 MB after all examples.

- Three examples (`native algorhythms`) grew past 2 GB on both machines: upstream `Z` calls an infinite stream finite, and `iseq` forced it to measure it. Fixed in sapf-sc (its docs/dev/upstream-sapf-bugs.md); all three now compile. The harness still evaluates each block first in a forked child with time and memory limits; the plugin cannot fork, so it needs the interrupt.

## Step 2 results

- sapf-sc: `ScsynthServer::now()` (default: the wall clock), `scsynthNow()`, which `play` and the ola scheduler read, and `setPostHandler`. The scheduler polls the server clock every 50 ms at most, so `stop` also ends its wait.

- ssp: `ScWorld::send`, `sendInBatches` (public) and `now()`; `plugins/sapf/Source/SapfServer` wraps them. One mutex orders `sendSync`: sapf syncs from its evaluation thread and each ola thread, and the reply ring has one reader. `send` needs none: scsynth's driver lock orders packets. `>sf` returns an error: an NRT render needs a World of its own.

- `server_test.cpp` runs the World on a thread at 1 or 2 times real time and plays single-impulse ola voices. `onsets.py` checks each onset against the `>nrt` score of the same program and seed: within one 64-frame block. Passes on the PC (`test_server.py`) and on the SSP (`run_server_on_ssp.sh`, 12 voices per speed). At 2 times real time, a server that reports the wall clock fails it.

- `log`, `log2` and `log10` of a negative input give NaN on the SSP, as in sapf, and finite values on the PC (nova-simd). `run_server_on_ssp.sh` reports both.

## Licence

GPL-3, as sapf and sapf-sc are. ttyp set the precedent in release 0.3.0.

## Plan

Each step ends with a check that must pass before the next.

1. **ARM build of the interpreter.** Done, except `scripts/fetch_sapf.sh`, which needs the sapf-sc changes committed to pin. Check passed; see Step 1 results.

2. **Server and clock.** Done; see Step 2 results.

3. **Keys.** Done. On the SSP (2026-10-14), with `keys_device` standing in for a second library (`plugins/common/tests/build_keys_device.sh`): the keyboard moved from ttyp to the stub when the stub was on screen and ttyp was not, and back when ttyp came on screen; ttyp kept it after switching away and back. ttyp on screen with the stub claiming kept the keyboard, as the rule says.

4. **Plugin.** Built: `plugins/sapf`. `Session` (World, interpreter thread, output, stop, memory watchdog), `TextBuffer` and `KeyQueue` carry no JUCE and are tested on the host (`test_session.py`). sapf-sc gained `gInterrupt`, checked per opcode and per list segment, and `runningTextures()`, so unloading waits for ola's detached threads. Check: [sapf-device-test.md](sapf-device-test.md).

## Decisions for v1

- Encoders: editor only. Mapping them to the running synth's `ctl` controls waits for sapf-sc's `@p` format (its next-steps item 4).

- MIDI: none.

- Block boundary: blank lines outside brackets and strings.
