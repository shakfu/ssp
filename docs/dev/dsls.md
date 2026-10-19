# Sequencer DSLs for an SSP plugin

Survey of the MIDI languages in [shakfu/midi-langs](https://github.com/shakfu/midi-langs) and [shakfu/psnd](https://github.com/shakfu/psnd), plus uSEQ, for a sequencer-oriented plugin that uses the `ScriptProcessor` code editor. Written 2026-10-17 from READMEs and headers only. Nothing was built. Real-time claims are inferred from the APIs.

## Recommendation

### 1. Bog (primary)

[shakfu/bog](https://github.com/shakfu/bog): a C port of [dogalog](https://github.com/danja/dogalog), a Prolog-based beat language. psnd embeds it.

- Model: each tick queries `event(Voice, Pitch, Vel, T)` for the next time slot. `event(kick,36,0.9,T) :- euc(T,4,16,4,0).` is a Euclidean kick. Patterns are declarative; there is no playback loop.

- External clock: `bog_scheduler_tick_at(sched, now_seconds)` (`src/bog/scheduler.h:62`) takes time from the caller. Synthor transport or a clock trigger can drive it.

- Live edit: `BogTransitionManager` swaps programs at the next quantization boundary (e.g. next bar). This matches Ctrl-Enter / Ctrl-E.

- Size and licence: ~3.5k lines of C, MIT, no dependencies beyond the MIDI front end.

- Changes needed:
  - `BogAudioCallbacks` hardcodes voices (`kick`, `snare`, `sine`, ...). Replace with one `note(ch, pitch, vel, dur, time)` callback.

  - Resolution calls `malloc` (23 sites). Run queries on a worker thread with lookahead. Push timestamped events to a FIFO that the audio thread plays at sample offsets.

### 2. Alda (secondary, for through-composed sequences)

Notation language (`piano: o4 c8 d e f g2`). Copy in psnd at `source/langs/alda`: ~18k lines, GPL-3.

- Fit: the interpreter compiles a score to a tick-stamped event array (`impl/finalize.h:29`, `alda_schedule_event`). Parse on the message thread and swap the array atomically. The audio thread reads by tick without allocating. Looping is tick modulo length.

- Changes needed: `context.h` includes libremidi directly. `alda.h` pulls in the async, TSF and MIDI backends. Both must be cut. Whether the core builds without libuv is unchecked.

- Weakness: patterns are static. Reacting to CV or triggers while playing does not fit the model.

### 3. uSEQ ModuLisp (best fit, if the licence clears)

[Emute-Lab-Instruments/uSEQ](https://github.com/Emute-Lab-Instruments/uSEQ): firmware for a eurorack live-coding sequencer. Paper: [Kyriakoudis & Kiefer, ICLC 2023](https://doi.org/10.5281/zenodo.7843874). Its language, ModuLisp, is a Lisp dialect specified in `docs/specs/MAIN.md`.

- Model: every expression in an output is an implicit function of time `t` (`signal-model.md` 1.1). `(d1 (sqr beat))` is a gate per beat. `beat`, `bar`, `phrase` are phasors derived from `t` and the `bpm` cell (`time.md` 1.2).

- Outputs: `a1`..`a8` continuous, `d1`..`d8` gates, `s1`..`s8` serial streams (`outputs.md` 1.1). These map onto SSP CV and trigger outs without translation.

- Real-time contract: the per-sample path never allocates or compiles. Compilation runs between ticks (`MAIN.md` 3.1). Fixed pools: `MAX_TOTAL_NODES = 360`, `MAX_CELLS = 256` (`signal_engine/types.h`).

- Live edit: a compile error keeps the previous program running. A runtime error or non-finite output falls back to the last known-good program (`MAIN.md` 2.1-2.2).

- Portability: the signal engine is ~6.4k lines of C++ shared by the RP2040 firmware and a WASM build. It includes only stdlib headers and `modulisp/lisp/symbol_intern.h`. Clock input: the host sets `ExecutionContext::t` each tick.

- Blockers:
  - Licence: the repo ships only CERN-OHL-S v2, a hardware licence. No software licence or file headers were found. Ask the authors before vendoring.

  - No MIDI output. Notes need a mapping layer, e.g. a `d` rising edge emits a note whose pitch comes from a paired `a` output.

  - Targets ~1 kHz control rate (`MAIN.md` 3.5). Per-sample cost on SSP is unmeasured. Running it once per block is likely enough for gates (inference).

#### Branches

The points above describe `main`. The most active branch, `dspengine` (9d2e535, 2026-10-15), has a different engine:

- No `signal_engine/` and no `docs/specs/`. Outputs run on the original tree-walking interpreter (`uSEQ/src/lisp/`).

- That interpreter allocates on every evaluation: `Value` holds `std::vector<Value>`, `ValuePtr` is `std::shared_ptr<Value>` (`lisp/value.h:19-21`). Not usable on the audio thread.

- New work is audio-rate generators built on DSPatch, run on the RP2040's second core (`uSEQ/src/dsp/uSeqGens/`): sampler, loop player, Euclidean, phasor, neural-net drum generator. The output stages target specific DACs (MEMLNaut, MT).

- MIDI out exists on both branches, but only in the legacy interpreter behind `#ifdef MIDIOUT`. `(mdo note fn)` samples `fn` at sub-bar steps and sends a channel-10 note-on whenever the value rises (`uSEQ.cpp:1115-1150`, dspengine). No note-off is sent. The `main` signal engine has no MIDI out.

Port `main`'s `signal_engine/` for the language. `dspengine`'s generators are tied to the hardware and duplicate what Synthor modules already do. The `mdo` rising-edge rule is a usable template for the MIDI mapping layer.

## Ranking

1. uSEQ, once the licence is clarified: CV and gate native, already real-time safe.

2. Bog: MIT, MIDI-shaped events, needs a worker thread and a callback rewrite.

3. Alda: only for written-out, looped parts.

## Rejected

- **joy-midi, stack-midi**: playback blocks on sleep (`common/midi_sleep.c`, `joy-midi/midi_primitives.c`). The timing would need rewriting into event scheduling.

- **MHS**: ~5.7 MB binary, ~2 s startup, garbage collector (psnd README).

- **Guile**: too large.

- **TR7, pForth**: no sequencing semantics; you write the sequencer.

- **Lua, s7, PocketPy**: general-purpose; same as above. See the alternative below.

## Alternative

Embed a general language (Lua 5.5 or s7) and write a pattern library on top, as Tidal does on Haskell and Strudel on JS. This costs designing the time model. It removes the limits of a fixed DSL. Choose it if Bog's Prolog model proves too narrow.

## Open questions

1. Clock source: Synthor transport, external clock trigger, or internal tempo? Bog supports all three. Alda's tick array assumes a tempo.

2. Overlap with `ttyp`: Teletype already covers imperative, trigger-driven scripts. Generative (Bog) or notated (Alda)?

3. Outputs: MIDI only, or also trigger and CV outs following the preset I/O template (in 1-8 trig, 9 CV; out 1-4 trig, 5-8 CV)? Bog voices map directly onto trigger outs.

4. Bog maturity: psnd calls it beta. Fuzz the parser and resolver for crashes and unbounded recursion before device testing. Unbounded queries on the worker thread are the main risk.

## Licences

midi-langs and psnd: GPL-3. bog: MIT. This repository is AGPL-3; GPL-3 modules (`ttyp`, `dexd`, `sapf`) already ship in it.

Implementation sketches: [dsl-useq.md](dsl-useq.md), [dsl-bog.md](dsl-bog.md), [dsl-alda.md](dsl-alda.md).
