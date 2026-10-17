# alda: Alda sequencer plugin (design)

Status: 2026-10-17, steps 1, 2 and 5 done; steps 3 (gates and CV) and 4 (MIDI out) built, device test pending ([alda-device-test.md](alda-device-test.md)). Choice of language: [dsls.md](dsls.md).

`alda` runs [Alda](https://alda.io) notation inside an SSP plugin. The interpreter compiles the buffer to a sorted array of MIDI events in ticks. The plugin loops that array against its own beat clock, and drives gates and pitch CV on SSP outs, and optionally MIDI.

Source: psnd's C implementation, `source/langs/alda/` (psnd 421cf5a, 2026-10-13). The core (`impl/`, 6.0k lines of C) is the parser, interpreter and `alda_events_finalize`. Playback (`scheduler.c`, `async.c`) and the backends (`backends/`) stay out: they sleep or use libuv. psnd's copy is newer than midi-langs' `projects/alda-midi` (9629737, 2026-10-07).

## Requirements

- Standalone: USB keyboard on the SSP, through `ScriptProcessor`'s code view.
- Ctrl-Enter replaces the score at the next loop boundary; Ctrl-E plays the block at the cursor once, from the next bar.
- A parse error leaves the running score playing.
- The audio thread never allocates or locks.
- Score tempo by default; plugin tempo, external clock and reset as options.

## Components

| Part | Source | Work |
|-|-|-|
| Scanner, parser, interpreter, finalize | psnd `langs/alda/impl/` and `scheduler.c` | fetched unchanged, pinned by commit (`scripts/fetch_alda.sh`) |
| Engine | `plugins/alda/Source/AldaEngine.{h,cpp}`, a `ScriptEngine` | new |
| Plugin, editor, keys | `ScriptProcessor`, `ScriptEditor`, `CodeKeys` | reused unchanged |
| Note output | events to gates, CV and MIDI | new; shared with [dsl-bog.md](dsl-bog.md) |
| Highlighting | psnd's `thirdparty/tree-sitter-grammars/tree-sitter-alda` | copied to `external/tree-sitter-alda` |

## Building the core

psnd's code builds unchanged; no fork is needed.

- `context.h` includes `<libremidi/libremidi-c.h>` for three pointer fields (`context.h:193-195`) that the core sets to NULL (`context.c:25-29`). `plugins/alda/Source/shim/libremidi/libremidi-c.h` declares the three types as incomplete structs.
- `finalize.c` calls `alda_schedule_event`, `alda_events_sort` and `alda_events_clear`, which live in `scheduler.c` beside `alda_events_play`, the MIDI player. `scheduler.c` is built; `Source/AldaCore.c` gives the five `alda_midi_*` functions the player calls empty bodies. The plugin never calls the player. `async.c` and `backends/` are not built.
- `scala.c`'s `g_error` is a global buffer (`scala.c:18`). Two instances loading Scala files at once would race on error text only. Left as is.
- `alda_interpret_ast` prints interpretation errors (an undefined variable, a note before any part) to stderr and returns -1. The plugin shows "cannot interpret the score". Parse errors come back as text with file, line and column. A psnd change returning the interpreter's message would fix this.

psnd's shared suite (20 scores) and conformance run (40 examples against Alda 2.4.7's output) pass against this build on the host (`test_alda.py`).

## Score

The interpreter records notes in seconds per part (`AldaNoteRecord`), since each part can have its own tempo. `alda_events_finalize` (`finalize.c:413`) assigns channels and converts to ticks at 480 per quarter note (`ALDA_TICKS_PER_QUARTER`), with the tempo map from the first part plus `(tempo! N)` (`context.h`, comment on `tempo_changes`). Parts at other tempi are converted through that one map.

The plugin uses the finalized `events` array, not the note records: ticks are beat positions (tick / 480 = quarter notes), so a beat clock plays them at any tempo.

A `Score` is immutable once built:

- `events`: note-on and note-off for voices 1-4 and tempo changes, in finalize's order (by tick; offs before ons at one tick). Built: `AldaEngine::Event`.
- `length`: the last event's tick rounded up to a bar (4 quarters); `# @loop N` sets it in quarters. Events at or past `length` are dropped.
- `bpm`: the tempo at tick 0.
- For MIDI: every part's notes with channel and velocity, and the program, CC 11 and pan (CC 10) events finalize places before a channel's notes. These repeat each loop. "Once per score" would be wrong: finalize can move a part to another channel mid-score and emits the settings where it does (`finalize.c:453-479`).

## Threads

- Worker (`ScriptEngine::idle`): a fresh `AldaContext` per compile, `alda_parse`, `alda_interpret_ast`, build a `Score`, publish it in an atomic pointer. A score not yet taken is replaced and freed.
- Audio thread: takes the published `Score` at the loop boundary, and hands the old one back through a second atomic slot. The worker frees it. While that slot is full, the swap waits a loop.

The interpreter allocates freely; it never runs on the audio thread. The audio thread reads one array with a cursor, so it needs neither lock nor allocation. At each restart (loop end, reset, swap) every voice releases; a gate open then stays closed for 1 ms (`GAP_MS`), so the next note retriggers. Each restart also sends note-off for every MIDI note still sounding, from a 16 x 128 bit set; the note's own off event later finds its bit clear and sends nothing.

## Time

The audio thread integrates a beat position per sample, at the current tempo.

- Tempo source, by `# @clock` line. Built 2026-10-17, except the control:
  - none (default): the score's tempo events, applied when the cursor passes them.
  - `# @clock [N]`: rising edges on In 2, N (default 4) per quarter note.
  - `# @clock midi`: MIDI clock, 24 per quarter note, with Start, Stop and Continue (`BaseProcessor::onMidiClock` and the transport hooks; `midiTransportInput(true)`).
  - Not built: a tempo control.

  Each pulse sets the position to the next pulse's (`lastGrid_` plus one pulse); between pulses the position advances at the last interval's rate and is clamped below the next pulse. Phase lock without a tempo estimator: uSEQ's `tempoEstimator` and sfct's `ClockTempo` average over beats, which this does not need, since every pulse corrects the position. Analog pulses land on their sample; MIDI ticks are applied at the start of the next block, so up to one block (2.7 ms) late, and the interval comes from the MIDI timestamps. A pulse that crosses the loop's end plays the loop's remaining events before wrapping.
- Reset: a rising edge on in 1, with teletype's thresholds (2.21 V high, 0.8 V low). Beat position 0, cursor to the first event, voices released. Built.
- Loop: at `length`, the cursor returns to 0; a pending score swaps in here.
- Ctrl-E: the block is interpreted alone (part declarations included, e.g. `piano: c d e`), and its events play once from the next bar, beside the loop. One overlay at a time; a new one replaces it.

## Engine

`AldaEngine : ScriptEngine`.

- `compile(text)`: interpret and build a `Score`; parse errors (`alda_parse`) become `error`.
- `runPart(block)`: build an overlay `Score`; `dropPart`/`dropAll` stop it.
- Comment: `#`, as `CodeKeys::comment`. `ScriptEngine::parseSpecs` accepts `@pN` after `;` or `//` only (`ScriptEngine.cpp:59`); pass it the marker, as Bog needs too.
- Controls `p1`..`p16`: Alda has no runtime values. Uses: `# @p1 transpose -24 24` style fixed meanings, applied by the audio thread to pitches and velocities. Which meanings is open question 2.
- `status()`: score length, loop position in bars, tempo, parts and their channels.

## I/O

Parts, in the order the score declares them, become mono voices 1-4. MIDI keeps every part and every note.

| Voice | Gate out | Pitch CV out |
|-|-|-|
| part 1 | 1, `Gate 1` | 5, `CV 1` |
| part 2 | 2, `Gate 2` | 6, `CV 2` |
| part 3 | 3, `Gate 3` | 7, `CV 3` |
| part 4 | 4, `Gate 4` | 8, `CV 4` |

The labels are the plugin's bus names, which Synthor shows on the jacks (`PluginProcessor::getBusesProperties`, read by `SSP_defaultDescriptor`).

- Gate: 1.0 (5 V) from note-on to note-off. Alda's quantization (default 90%, `ALDA_DEFAULT_QUANT`) leaves a gap between notes, so repeated notes retrigger.
- Pitch CV: 1 V/octave, MIDI 60 = 0 V, clamped to the SSP's +-5 V (MIDI 0..120). It holds after the gate ends.
- Chords: the last note-on wins. Release returns to the note still held, if any.
- Percussion parts (channel 10): as any part, a gate per note and the note as pitch CV.
- Not built: `# @voice 2 part bass`, to map a part by name instead of order.
- Inputs: in 1 resets; a clock input is not built.

## MIDI out

Built. The audio thread queues 2- or 3-byte messages in a `readerwriterqueue` of 1024 (`AldaEngine::midiOut`); a full queue drops and counts. A thread of the plugin's drains it every 1 ms to two places: the plugin's own ALSA output port, and `BaseProcessor::sendMidiMessagesNow`, which writes to the general panel's MIDI out device, if one is chosen, under the device lock. Removing the module sends All Notes Off on all 16 channels.

The port is `juce::MidiOutput::createNewDevice("alda")`, on an ALSA client named `alda` (`JUCE_ALSA_MIDI_NAME`; JUCE's default is `JUCE`). Each plugin `.so` links its own JUCE, so the port is a client of its own; other modules' MIDI IN lists enumerate every client's ports (`juce_Midi_linux.cpp:470-477`) and show it. The general panel hides names containing `Juce` or `Midi Through Port` only (`SystemEditor.cpp:13-14`), and tests the port name. Instances number their ports `alda`, `alda 2`, reusing the lowest free number. `test_alda_midi_port` reads the port with `aseqdump` on the host.

The messages carry no frame offset: a block's messages leave together, within 1 ms of the block. Timing is then block-quantized (2.7 ms at 128 frames) plus up to 1 ms. Sending with timestamps through `sendBlockOfMessages` would remove the quantization; not done until the device test shows it matters.

`sendMidiMessagesNow` dereferenced the device unchecked and without the lock, and no plugin called it before; both are fixed.

Synthor calls a module's `processBlock` only while one of its jacks is patched, if only to an `INPT` module with nothing in its hardware jack (seen on the SSP, 2026-10-17: unpatched, `alda`'s port sent nothing to a monitor; with `INPT` IN1 to `alda` In 1, `dexd` played). `alda`'s clock is `processBlock`, so an unpatched `alda` sends no MIDI. Whether an output alone suffices is untested.

The first version sent to the general panel's device only. On the SSP (2026-10-17), `dexd`'s MIDI IN then listed only the USB keyboard: `alda` created no port, and Synthor routes no MIDI between modules.

## Highlighting

psnd's `tree-sitter-alda`, copied unchanged to `external/tree-sitter-alda` (ABI 14; the runtime takes 13 to 15). It has no LICENSE file; it comes from psnd (GPL-3) and is your own code (inference: no upstream URL in its `package.json`). Its query captures attribute names as `@preprocessor`, which `Highlighter` leaves plain. `highlight_test.cpp` covers it.

## Tests

Built and run by `plugins/alda/tests/test_alda.py` under `make test`:

- psnd's shared suite and conformance run against the core built here.
- `alda_test.cpp`, natively:
  - `piano: c4 d e f` at 120 bpm: gate 1 rises at frames 0, 24000, 48000, 72000, within one frame; pitch CV 0, 2/12, 4/12, 5/12 V. The loop's second pass starts at 96000.
  - Gates stay open 90% of a note.
  - Two parts on outs 1-2 and 5-6; a chord's last note sets the pitch.
  - `# @loop 2` wraps after two quarters; a bad `@loop` is an error.
  - A new score mid-loop starts when the loop ends.
  - A parse error leaves the score playing, with `error()` set.
  - Reset on in 1 during a note: the gate closes for 1 ms, then the loop restarts.
  - `(tempo! 60)` doubles the frames.
  - The built-in score compiles and plays.
  - Each file in `plugins/alda/examples` builds.
- The same test under ThreadSanitizer: a worker compiling in a loop while the audio thread plays.
- `plugins/common/tests/test_engine_plugins.py`: the built `alda.so` through the SSP API, the built-in and a loaded score.

- MIDI, in `alda_test.cpp`: the settings and notes in order for `piano: c4 d`; a reset releases the sounding note and replays the settings; a fifth part and a percussion part (channel 10) play as MIDI only; a note cut at the loop's end is released once. Under ThreadSanitizer, a consumer thread drains the queue as the plugin's does.

- Stop, and the empty score that the editor's reset runs (`testStop`, `testEmptyStops`); Ctrl-L, Ctrl-L twice, Ctrl-., Ctrl-R and Ctrl-R then Ctrl-L over the key port (`test_alda_stop_clear_reset`).
- Clocks: In 2 at 234.375 bpm, the d and its measured tempo, the halt without pulses; MIDI clock with Start, Stop (releasing the note), Continue and a second Start; the `@clock` comment's forms.

Still to test: Ctrl-E overlay.

## Risks

| Risk | Evidence | Mitigation |
|-|-|-|
| Static patterns | Alda is notation; no runtime inputs | p-control meanings (open question 2); Bog covers generative use |
| Parts at different tempi drift | one tempo map converts all parts (Score) | document; warn when parts declare different tempi |
| Interpret time for long scores | 5.1 ms at most for psnd's examples on the SSP (step 2) | worker only |

## Plan

1. **Core on the host.** Done: built unchanged (Building the core); the suites pass.
2. **Core on the SSP.** Done: `plugins/alda/tests/run_on_ssp.sh` runs both suites and `alda_test` on the SSP; all pass. Build times, Synthor running: 5.1 ms for `all-instruments.alda` (6.7 KB), 2.5 ms for `bach_cello_suite_no_1.alda`; the rest under 3 ms.
3. **Plugin.** Gates, CV, loop, swap, reset: built for the SSP. Check: [alda-device-test.md](alda-device-test.md). Ctrl-E overlay, tempo sources and `p1`..`p16` not started.
4. **MIDI out.** Built. Check: [alda-device-test.md](alda-device-test.md), steps 20-25.
5. **Highlighting.** Done.

## Open questions

1. Home of the code: a fork of psnd's Alda as its own repository, or fetch psnd's subtree. midi-langs and psnd already share it, so one upstream would serve all three.
2. Controls: which fixed meanings for `p1`..`p16`? Transpose, velocity scale and swing are candidates. Or none, and Alda stays a pure player.
3. Loop or one-shot: should a score without `# @loop` play once and stop?
4. Name: `alda` fits the 4-character rule. alda.io is a separate project (Clojure/Go); the name may read as an official port.

## Licence

psnd and midi-langs are GPL-3, so the plugin is GPL-3, as `dexd` and `ttyp` are inside this AGPL-3 repository. The C implementation is a reimplementation of the Alda language, not a copy of alda.io's code (inference from the repositories; alda.io's own licence unchecked).
