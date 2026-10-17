# mlsp: ModuLisp sequencer plugin (design)

Status: sketch, 2026-10-17. Nothing built. Blocked on the licence (Licence). Choice of language: [dsls.md](dsls.md).

`mlsp` runs ModuLisp, the language of the [uSEQ](https://github.com/Emute-Lab-Instruments/uSEQ) eurorack module, inside an SSP plugin. You type ModuLisp on a USB keyboard and evaluate it. Each output is a function of time; the plugin samples it and drives SSP gate and CV outs, and optionally MIDI.

Source: uSEQ `main` (b628d0e, 2026-05-06), `uSEQ/src/signal_engine/`. Not `dspengine`: its interpreter allocates on every evaluation ([dsls.md](dsls.md), Branches). Spec references below are to `docs/specs/` on `main`.

## Requirements

- Standalone: USB keyboard on the SSP, through `ScriptProcessor`'s code view.
- Ctrl-Enter runs the buffer; Ctrl-E runs the form at the cursor beside it.
- A compile error never stops the outputs (spec `MAIN.md` 2.1).
- Gate edges accurate to a control tick (Control rate).
- Internal clock; external clock and reset from inputs.
- SSP preset I/O template: trigger outs before CV outs, appended, never reordered.

## Components

| Part | Source | Work |
|-|-|-|
| Compiler, executor | uSEQ `signal_engine/` (~6.4k lines, C++17, stdlib only) and `modulisp/lisp/symbol_intern.h` | fork `shakfu/uSEQ`, pin by commit, `scripts/fetch_useq.sh` |
| Tempo from clock input | uSEQ `dsp/tempoEstimator.{h,cpp}` | same fork |
| Engine | `plugins/mlsp/Source/UseqEngine.{h,cpp}`, a `ScriptEngine` | new |
| Plugin, editor, keys | `ScriptProcessor`, `ScriptEditor`, `CodeKeys` | reused unchanged |
| Highlighting | a tree-sitter grammar for a Clojure-like Lisp, plus a `highlights.scm` built from `symbols.def` | new under `external/` (Highlighting) |
| MIDI out | `BaseProcessor`'s MIDI out device; `external/readerwriterqueue` | new (MIDI out) |

## Threads

uSEQ's firmware is single-threaded: `Firmware::tick()` runs `eval_cold`, then `execute_all_outputs`, `commit_state`, `commit_outputs` (`firmware/firmware.cpp:84`). The plugin splits that loop:

- Worker (`ScriptEngine::idle`): `compile()` and `runPart()` call `sig::eval_cold` on the one `SignalEngine`, holding `engineLock_`.
- Audio thread: `process()` takes `engineLock_` with `try_lock`. On success it runs the ticks for the block. On failure it holds the last output values for the block.

A `try_lock` over a mutex was chosen over double-buffering the `NodePool`. With double-buffering, the audio thread advances state slots, `prev` buffers and `rate-as` clocks in its copy while the worker compiles against stale ones; uSEQ's state-identity rules (`state-identity.md`) would then need a merge step in the plugin. With the mutex, uSEQ handles continuity itself. The cost is a held block when a compile overlaps one. Spec `MAIN.md` 3.2 gives recompilation "well under one millisecond"; a 128-frame block is 2.67 ms. Step 2 measures it on the SSP. If held blocks are audible, double-buffer then.

`eval_cold` must not allocate on the audio thread; it never runs there. The executor path is allocation-free (`MAIN.md` 3.1). The one allocation found, `NodePool::batch_workspace` (`node_pool.cpp:480`), is for visualisation and is not used.

## Engine

`UseqEngine : ScriptEngine`, as `ChuckEngine` and the Csound engine are.

- `compile(text)`: `(useq-clear)`, then `eval_cold(text)`. `eval_cold` handles several top-level forms as an implicit `do` (`cold_eval.cpp:994`). Diagnostics (`EvalResult::diagnostics`, with source spans) become `error`, with a line number.
- `runPart(part)`: `eval_cold(part)` without the clear. Redefining an output or a cell is uSEQ's live-edit model (`live-edit.md`), so parts need no removal: `dropPart` and `dropAll` run `(useq-clear)`.
- `builtin()`: a short program using each output family, shipped as the default.
- Comment: `;`. The `; @p1 name min max` lines of `ScriptEngine` work unchanged.
- Controls: `p1`..`p16` become cells of those names, defined at `prepare()`. Each block the audio thread writes `value(i)` into the cell snapshot after `cells.snapshot_values()`, so the controls need no lock. The symbol IDs are resolved once on the worker.
- `status()`: tempo, `bar-num`, and each assigned output's health (`idle`/`running`/`fallback`/`error`, `failure-model.md`).

### Control rate

uSEQ ticks at ~1 kHz (`MAIN.md` 3.5). The plugin runs one tick every `TICK` frames: 16 at 48 kHz is 3 kHz, 0.33 ms resolution. Gates switch at the tick's first frame. CV outs hold between ticks; a one-pole smoother is added only if steps are audible on pitch CV. `TICK` is a constant until Step 2 measures the cost per tick.

### Time

uSEQ's `t` is seconds since transport start (`time.md` 1.1). No plugin in this repo reads a host playhead, so the plugin keeps its own:

- `t` advances by `TICK / sampleRate` per tick while running; `dt` is that step.
- Run/stop: a button, and `(set-bpm ...)` and the other timing forms from code.
- Reset input: a rising edge sets `t = 0`.
- Clock input: rising edges feed `tempoEstimator`, which sets the `bpm` cell. Off unless a `; @clock N` line names the input.

## I/O

Engine output indices are `a1-a8 = 0-7`, `d1-d8 = 8-15`, `s1-s8 = 16-23` (`firmware.cpp:159`).

| SSP jack | ModuLisp | Scale |
|-|-|-|
| out 1-4 | `d1`-`d4` | gate: above 0.5 is 5 V (1.0), else 0 (`outputs.md` 1.1) |
| out 5-8 | `a1`-`a4` | 0..1 to 0..5 V, clamped |
| in 1-8 | `in1`-`in8` | gate: above 1 V is 1 |
| in 1-8 | `ain1`-`ain8` | the same jack, 0..5 V to 0..1 |

`a5`-`a8`, `d5`-`d8` and `s1`-`s8` have no jack; MIDI uses them. The 0..5 V CV range is a first choice, not checked against uSEQ hardware; a per-plugin range switch, as `ttyp` has, follows if needed.

Fork patch: `input_index_for_name()` knows `in1`, `in2`, `ain1`, `ain2` only (`inputs.md` 1.2-1.3). Extend it to 8 of each. An unknown input is a compile error (`inputs.md` 1.9), so the patch is visible to users.

## MIDI out

Phase 2, after the gate and CV plugin passes its device test. No fork patch: four voices use the outputs without jacks.

- Voice `k` (1-4) is `d(k+4)` gate, `a(k+4)` note number, `s(k+4)` velocity 0..1. MIDI channel `k`.
- A rising edge on the gate sends note-on with `round(a)`, clamped to 0..127. A falling edge sends note-off for the note that was sent.
- A note change while the gate is high sends nothing. Legato needs a second rule; omitted until wanted.

This follows the `dspengine` branch's `(mdo note fn)` rule, a rising value sends note-on (`uSEQ.cpp:1115-1150` there). That rule sends no note-off and only drum notes.

Delivery: the audio thread pushes `(frame, bytes)` into a `readerwriterqueue`. A MIDI thread drains it into `MidiOutput::sendBlockOfMessages`, on the device `BaseProcessor` opens with its background thread. Not from the audio thread: `sendBlockOfMessages` locks and copies. Not from the worker: `IDLE_MS` is 10, so jitter would reach 10 ms. Step 4 measures jitter.

## Highlighting

uSEQ has no tree-sitter grammar; its editor is in `interfaces/useqedit`. ModuLisp's syntax is Clojure-like: `()` lists, `[]` vectors, keywords. Candidates:

- An existing Clojure grammar, e.g. [sogaiu/tree-sitter-clojure](https://github.com/sogaiu/tree-sitter-clojure), with a `highlights.scm` written here. Its licence is unchecked.
- A minimal grammar of our own: atoms, lists, vectors, comments, strings.

Either way, `highlights.scm` lists the builtins and output names from `symbols.def` (194 lines), as `@function.builtin` and `@variable.builtin`. `ssp_highlights(MLSP <grammar>)` builds it, as for `chuk`.

## Tests

Host first, then SSP, as for `sapf`.

- `plugins/mlsp/tests/engine_test.cpp`, built natively, with `test_mlsp_engine.py` as its pytest wrapper under `make test`:
  - `(d1 (sqr beat))` at 120 bpm: a rising edge every 24000 frames, within one tick.
  - A syntax error keeps the previous program's outputs; `error()` names the line.
  - Ctrl-E on `(d2 ...)` leaves `d1` running.
  - `p1` reaches a cell: `(a1 p1)` follows `setParam`.
  - Reset input sets `bar-num` to 0; a clock input at 140 bpm sets `bpm` within 2 bpm.
  - `try_lock` failure holds outputs for exactly one block.
  - MIDI voices: note-on and note-off pairs, timestamps within one tick.
- uSEQ's own suite (`test/signal_engine/`) on the host, and cross-built for the SSP's 32-bit ARM. Run once per fork pin.
- ThreadSanitizer on the engine test.

## Risks

| Risk | Evidence | Mitigation |
|-|-|-|
| Licence never granted | no software licence in the repo (Licence) | steps 1-2 stay local; stop before step 3 |
| Compile holds outputs audibly | `try_lock` design; compile time on SSP unknown | measure in step 2; double-buffer the `NodePool` |
| Per-tick cost | 360-node pool, 24 outputs per tick; SSP cost unknown | raise `TICK`; skip unassigned outputs (`classify_outputs`) |
| Fixed pools too small for SSP programs | `MAX_TOTAL_NODES = 360`, `MAX_CELLS = 256` (`types.h`), sized for RP2040 RAM | raise in the fork; the SSP has 2 GB |
| Spec ahead of code | `q0` is in `outputs.md` 1.6 but absent from `signal_engine/` | document only what `main` implements |
| Upstream churn | `main` and `dspengine` diverge; specs are recent | pin by commit; re-run uSEQ's suite per pin |
| One engine per instance | `SignalEngine` has no globals found so far (unverified) | check in step 1; second instance test |

## Plan

Each step ends with a check that must pass before the next. Steps 1-2 fetch uSEQ into `build/` and commit none of its code.

1. **Engine on the host.** `scripts/fetch_useq.sh`, a CMake harness linking `signal_engine/` and the fork patch for 8 inputs. Check: uSEQ's suite passes; the harness renders `(d1 (sqr beat))` with correct edges.
2. **Engine on the SSP.** Cross-build the harness with `xcSSP.cmake`. Check: uSEQ's suite passes on ARM; ticks per second and `eval_cold` time for the examples measured, and `TICK` chosen.
3. **Plugin.** `plugins/mlsp`: `UseqEngine`, `CodeKeys` (port, `MLSP_KEYBOARD`, `.useq`, `;`), I/O, clock, reset. Check: the host tests above, then a device test document.
4. **MIDI out.** Voices, queue, MIDI thread. Check: note pairs on a MIDI monitor; jitter measured.
5. **Highlighting.** Grammar and `highlights.scm`. Check: `highlight_test.cpp` covers ModuLisp.

## Open questions

1. Name: `mlsp` (the language) over `useq` (Emute's product). `useq` may read as an official port. Either fits the 4-character rule.
2. Fork or patch set: fork `shakfu/uSEQ` as for sapf, or keep patches in this repo and apply them at fetch.
3. Gate polarity and CV range against your uSEQ module: does `a` 0..1 span 0-5 V there?
4. Does `dspengine` merge onto the signal engine? If so, re-pin after the merge, not before.

## Licence

Unresolved; email sent to Emute Lab, 2026-10-17. The repo carries only CERN-OHL-S v2 (`license/`), a hardware licence; no software licence or source headers were found. Code published without a licence is not open source: GitHub's terms let others view and fork it on GitHub, nothing more. Owning the module does not change this. The reply should state the licence of `uSEQ/src/`. If it is CERN-OHL-S v2, check its compatibility with this repository's AGPL-3 code before step 3; that is not yet researched.
