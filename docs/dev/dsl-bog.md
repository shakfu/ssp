# bogl: Bog sequencer plugin (design)

Status: sketch, 2026-10-17. Nothing built. Choice of language: [dsls.md](dsls.md).

`bogl` runs Bog, a C port of [dogalog](https://github.com/danja/dogalog), inside an SSP plugin. A Bog program is Prolog clauses for `event(Voice, Pitch, Vel, T)`. The scheduler queries them at each grid step, and each solution becomes a note: a trigger and pitch CV on SSP outs, and optionally MIDI.

Source: psnd's copy, `source/langs/bog/impl/` (psnd 421cf5a, 2026-10-13, 3.6k lines of C11). It is ahead of [shakfu/bog](https://github.com/shakfu/bog) (31ae578, 2026-01-17): it adds source-line tracking (`bog_scheduler_get_current_source_line`). Open question 1 picks the home.

## Requirements

- Standalone: USB keyboard on the SSP, through `ScriptProcessor`'s code view.
- Ctrl-Enter replaces the program at the next bar; Ctrl-E adds the clause block at the cursor.
- A compile error, or a query that fails at run time, never stops the running program.
- No query can hang or crash Synthor (Fork changes).
- Internal clock; external clock and reset from inputs; tempo changes without phase jumps (Time).

## Components

| Part | Source | Work |
|-|-|-|
| Parser, resolver, builtins, scheduler | psnd `langs/bog/impl/` | fork, pin by commit, `scripts/fetch_bog.sh`; patches below |
| Engine | `plugins/bogl/Source/BogEngine.{h,cpp}`, a `ScriptEngine` | new |
| Plugin, editor, keys | `ScriptProcessor`, `ScriptEditor`, `CodeKeys` | reused unchanged |
| Note output | events to gates, CV and MIDI | new; shared with [dsl-alda.md](dsl-alda.md) |
| Event queue | `external/readerwriterqueue` | present |
| Highlighting | a tree-sitter grammar for Bog's Prolog subset | new (Highlighting) |

## Fork changes

Found by reading the code; each needs a test in the fork.

- **Generic voice callback.** `trigger_voice` matches the voice atom against eight names with `strcmp` and drops every other atom (`scheduler.c:256-294`). Replace with one `note(userdata, voice, time, pitch, vel)` callback. The plugin maps atoms to outputs (I/O).
- **Resolution limits.** `resolve_internal` recurses with no depth limit (`bog.c:1390-1449`). `event(V,P,X,T) :- event(V,P,X,T).` overflows the worker's stack and kills Synthor. Add a depth limit, a solution cap and a step budget per query. A query over budget is an error: the plugin reverts to the previous program.
- **PRNG.** `choose`, `chance`, `rand`, `randint` call `rand()` (`builtins.c:321-421`). It is one global stream shared by every instance and thread, unseeded, so each boot plays the same "random" sequence. Move a PRNG into `BogContext`, seeded per instance; a `% @seed N` line fixes it.
- **Transition boundary.** `bog_transition_manager_process` swaps programs when `now` reaches the boundary (`scheduler.c:654`). The scheduler has already queried `lookahead` ahead with the old program, so that window plays the old program after the bar line. Compare the boundary against the query time instead.

## Threads

- Worker (`ScriptEngine::idle`, every `EngineProcessor::IDLE_MS` = 10 ms): parse, `bog_scheduler_tick_at`, and the transition manager. Each solution's callback pushes `(beat, voice index, pitch, vel)` into an SPSC `readerwriterqueue`.
- Audio thread: pops events into a fixed sorted buffer (1024 events; overflow counted, not allocated). It fires each when the beat clock reaches it, at a sample offset within the block.

Resolution allocates (arenas per query, `scheduler.c:463-546`); it never runs on the audio thread. The lookahead must cover the worker's period plus a compile: 80 ms (Bog's default, `scheduler.c:340`) is 8 periods. A compile that runs past the lookahead leaves a gap in the events; the status line counts late events.

## Time

Bog times are seconds; builtins derive beats as `T * bpm / 60` (`builtins.c:440`). A tempo change then moves the beat position of `now`, and the scheduler's `last_scheduled_time` skips or repeats steps.

The plugin runs the scheduler at a fixed 60 bpm, so Bog's `T` equals beats. It integrates its own beat position per sample from the real tempo, and passes that beat position to `bog_scheduler_tick_at`. Events come back in beats, and the audio thread fires them against the same clock. Tempo changes then change only the rate. Lookahead is set in beats each tick: `0.08 * bpm / 60`. This needs no fork patch; it assumes no builtin exposes `bpm` to programs (none found).

- Tempo: a control, or `% @bpm N`.
- Clock input: rising edges set the tempo, with a `% @clock N ppqn P` line; uSEQ's `tempoEstimator` is one model.
- Reset input: beat position 0, scheduler `last_scheduled_time` cleared.
- Grid: `grid_beats` 0.25 by default (`scheduler.c:341`); `% @grid 0.125` changes it.

## Engine

`BogEngine : ScriptEngine`.

- `compile(text)`: `bog_validate_code`, then the transition manager at the next bar (4 beats; `% @bar N` changes it). The parse error becomes `error`.
- `runPart(block)`: the block's clauses join the program, re-validated as one text. `parts()` lists them by first voice atom; `dropPart`/`dropAll` remove them. This is psnd's slot model (`:def kick ...`) without names.
- Comment: `%`, as `CodeKeys::comment`. `ScriptEngine::parseSpecs` accepts `@pN` lines after `;` or `//` only (`ScriptEngine.cpp:59`). Give it the comment marker as an argument, shared with Alda's `#` ([dsl-alda.md](dsl-alda.md)).
- Controls `p1`..`p16`: Bog has no variables to set from outside. Add builtin `param(N, X)`, binding `X` to control `N`'s value (a fork patch, small). `event(hat, 42, V, T) :- every(T, 0.5), param(1, V).` then follows knob 1.
- Gate length: a control, as a fraction of the grid step. Bog events have no duration.
- `status()`: tempo, beat, the clause line that fired last (`bog_scheduler_get_current_source_line`), late and dropped events.

## I/O

Voices map to outputs by atom. Defaults follow psnd's MIDI map (`register.c:6-12`), so psnd programs play unchanged:

| Voice | Gate out | Pitch CV out | MIDI channel |
|-|-|-|-|
| `kick` | 1 | - | 10 |
| `snare` | 2 | - | 10 |
| `hat` | 3 | - | 10 |
| `clap` | 4 | - | 10 |
| `sine` | - | 5 | 1 |
| `square` | - | 6 | 2 |
| `triangle` | - | 7 | 3 |
| `noise` | - | - | 10 |

- `% @voice bass gate 4 cv 8 ch 5` adds or remaps an atom. An unmapped atom is a warning on the status line.
- Gate: 1.0 (5 V) for the gate length, else 0.
- Pitch CV: 1 V/octave, MIDI 60 = 0 V, clamped to the SSP's +-5 V (MIDI 0..120). It holds after the gate ends.
- Pitch: the event's own `Pitch` for every voice. psnd ignores it for drums and sends fixed GM notes (`register.c:40`); here the program decides.
- Inputs: a Bog builtin `in(N, X)` would bind input N's level (fork patch). Deferred until a program needs it.

## MIDI out

Same delivery as [dsl-useq.md](dsl-useq.md), MIDI out: the audio thread pushes `(frame, bytes)` into a second `readerwriterqueue`; a MIDI thread drains it into `MidiOutput::sendBlockOfMessages` on `BaseProcessor`'s device. Note-on at the event, note-off after the gate length. psnd sends note-on at query time and ignores `time` (`register.c:139-147`), so its notes play up to 80 ms early; the plugin uses the time.

## Highlighting

psnd's `tree-sitter-prolog` (from codeberg.org/tree-sitter-prolog) is AGPL-3, as this repository, so it can be vendored. It parses full Prolog; Bog's syntax is a small subset (atoms, variables, numbers, lists, `:-`, infix arithmetic and comparison, `%` comments). Check that it parses the Bog examples before writing a grammar of our own. `highlights.scm` lists the 32 builtins (`builtins.c:748`) as `@function.builtin`.

## Tests

- `plugins/bogl/tests/engine_test.cpp`, built natively, with `test_bogl_engine.py` under `make test`:
  - `event(kick,36,0.9,T) :- every(T,1.0).` at 120 bpm: gate 1 rises every 24000 frames, within one frame.
  - Tempo 120 to 90 mid-bar: no skipped or doubled step.
  - Ctrl-Enter mid-bar: the new program starts on the next bar, not one lookahead late.
  - The self-recursive clause above: an error, the previous program keeps playing, the worker survives.
  - Two instances with the same seed play the same sequence; different seeds differ.
  - Reset input: next event at beat 0.
  - MIDI: note-on and note-off pairs at the event frames.
- The fork's own tests (`tests/test_*.c`), on the host and cross-built for the SSP.
- ThreadSanitizer on the engine test.

## Risks

| Risk | Evidence | Mitigation |
|-|-|-|
| Runaway query crashes Synthor | no depth limit (Fork changes) | depth, solution and step limits; fuzz the parser and resolver |
| Query cost per grid step on SSP | arenas and `malloc` per query and per solution (`scheduler.c:463-546`) | measure in step 2; coarser grid; raise lookahead |
| Late events after a slow compile | worker does both | count late events; compile into a second scheduler and swap |
| Beta implementation | psnd README calls Bog beta | fork tests plus fuzzing before the device test |
| Swing uses bpm | `swing_adjust(t, bpm, ...)` (`scheduler.c:248`) | correct at the fixed 60 bpm, since `t` is beats |

## Plan

1. **Fork on the host.** Fetch, the four fork changes, `param/2`. Check: fork tests pass; the recursion and seed tests above pass; a fuzz run of the parser and resolver of an hour with no crash.
2. **Fork on the SSP.** Cross-build. Check: tests pass on ARM; query time per grid step for the examples measured.
3. **Plugin.** `plugins/bogl`, beat clock, voice table, gates and CV. Check: host tests, then a device test document.
4. **MIDI out.** Check: note pairs on a MIDI monitor; jitter measured.
5. **Highlighting.** Check: `highlight_test.cpp` covers Bog.

## Open questions

1. Home of the code: port psnd's changes back to `shakfu/bog` (MIT) and fetch that, or fetch psnd's subtree (GPL-3). The first keeps one upstream for psnd and this repo.
2. Name: `bogl`, since a plugin's name must be its 4-character `PLUGIN_CODE`. `bog` alone is 3.
3. Event duration: keep a gate-length control, or add an optional fifth argument, `event(V,P,Vel,T,Dur)`, in the fork.
4. Is the default voice table right for your patches, or should outs 1-4 be pitched voices with gates?

## Licence

MIT for `shakfu/bog`; psnd is GPL-3. dogalog, the original, is unchecked. Built from psnd's copy, the plugin is GPL-3, as `dexd`; from `shakfu/bog`, it can be AGPL-3, as this repository.
