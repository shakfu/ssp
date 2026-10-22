# gseq: grid sequencer (sketch)

Status: design, 2026-10-22. Nothing built. Decided: polymeter, retrigger on overlap, velocity CV per row, and the clock extracted from alda first (see [Step 1](#step-1-extract-the-clock)).

A step sequencer shown as a grid. Each row has its own length. Each step holds a note, a velocity and a duration. A row plays to MIDI, to CV jacks, or both. The grid is edited with the encoders, without code.

## Model

- **Pattern:** up to `ROWS` rows, a tempo, a clock source, swing.
- **Row:** a length of 1-32 steps, a division (steps per quarter note), a target, a mute.
  - Target MIDI: a channel 1-16.
  - Target CV: a voice, which is a gate jack and a pitch jack.
- **Step:** on/off, note (0-127), velocity (1-127), duration in steps (0.1-8).

Rows of different lengths drift against each other and realign after their least common multiple. A 16-step row against a 12-step row realigns every 48 steps. This is polymeter: every row's step lasts 1 / division quarter notes, whatever its length. A row's step is `floor(pos / ticksPerStep) mod length`, from the clock's position in ticks.

## Row limits

- **CV:** 4 voices. Outputs `Gate 1-4`, `CV 1-4`, then `Vel 1-4`: 12 jacks, in that order, so the first 8 match alda's and the user's preset template (gates, then CVs). sfct already has 14 outputs, so 12 fit.
- **Velocity CV:** a per-row setting, off or on. On: `Vel k` carries the step's velocity, 127 at 5 V, held until the next step. Off: `Vel k` stays at 0 V. The jacks are fixed either way, because Synthor's jacks cannot change with a setting.
- **MIDI:** the protocol imposes no row limit. Rows can share a channel: drum rows all on channel 10, one note each. The real limit is the screen. 8 rows at 46 px each fill the grid area.

So `ROWS = 8`. A row targets MIDI, a CV voice, or both, and at most 4 rows target CV.

## Screen

Grid area: 1600 x 370 px, above the button row (as the script Code view, 376 px).

- 32 columns of 48 px, 8 rows of 46 px. Steps past a row's length are blank.
- Cell: filled when on. Brightness = velocity. A bar from the cell's left edge = duration, running into the next cells when longer than 1 step. The note name is in small text.
- Playhead: one column per row, since rows differ in length.
- A row label on the left: length, division, target (`ch10`, `cv2`).

Editing, with the existing controls (4 encoders, 8 buttons, arrows):

| Control | Does |
|-|-|
| arrows | move the cursor between steps and rows |
| encoder 1 | note; press toggles the step |
| encoder 2 | velocity |
| encoder 3 | duration |
| encoder 4 | row length; press cycles the division |
| button | row page: target, channel or voice, mute |
| button | copy row / paste row |

A USB keyboard could map to the same actions through `KeyInput`, with port 7776 next after alda's 7775. That is optional for v1.

## Reuse

- **Clock:** `AldaEngine` already has free tempo, a clock input with ppqn, MIDI clock with Start/Stop/Continue, a reset input, and swing (`plugins/alda/Source/AldaEngine.h`). It moves to `plugins/common/engine` first; see Step 1.
- **MIDI out:** alda's pattern. One ALSA port per instance (`MidiOutput::createNewDevice`, `alda`, `alda 2`, ...), fed from a lock-free queue on a 1 ms thread (`midiLoop`).
- **CV:** 1 V/octave, C4 (MIDI 60) at 0 V, 0.2 per volt, as alda (`plugins/alda/README.md`). Pitch holds after the gate closes.
- **Host:** `EngineProcessor` for the worker thread and block splitting. No `ScriptEngine`: there is no program text.
- **State:** the pattern in the preset's custom XML, as one attribute per row. No files for v1.

## Failure modes

- **Synthor skips unpatched modules.** `processBlock` runs only while a jack is patched. A MIDI-only gseq with no cables would never run. The workaround is a virtual patch from INPT, which needs documenting or an automatic fix.
- **Overlaps.** A duration past the next step's start overlaps. Decided: retrigger. On a CV row, the gate closes for 1 ms (alda's `GAP_MS`) and the new pitch and velocity apply. On MIDI, a note-off for the sounding note goes before the new note-on, also when the two notes are equal.
- **Timing.** Events quantize to the audio block: 2.67 ms at 128 frames. alda's 1 ms MIDI poll adds up to 1 ms of jitter. Both are acceptable for a step sequencer; this is an inference, not measured.
- **Edits while playing.** The pattern needs the same handoff alda uses for scores (pending and retired pointers), so the audio thread never frees memory.
- **Pitch range.** +-5 V covers MIDI 0-120 at C4 = 0 V. Notes above 120 clip on CV but not on MIDI.

## Alternatives

1. **Drum grid.** Each row is one fixed note; a step is on/off plus velocity. This is the more common "simple visual grid" and needs one encoder, not three. The proposed model already includes it: a row whose steps share one note. A per-row "fixed note" flag would make that the default.
2. **Polyrhythm instead of polymeter.** Every row spans the same bar, so 12 steps are triplets against 16. That is one flag per row: division from length.
3. **No new module.** alda already drives 4 CV voices and MIDI. A grid editor that writes alda text keeps one engine. Against it: alda text has no grid, and round-tripping edits through text loses the cursor model.
4. **ttyp patterns.** Teletype has 4 patterns of 64 values with lengths. That covers note-only rows, but not velocity or duration per step.

Recommendation: the proposed model, with alternatives 1 and 2 as row flags. Both cost little and cover the common grid uses.

## v1 scope

8 rows, 32 steps, per-row length and division, note, velocity and duration, MIDI and CV targets, velocity CV per row, shared clock, preset storage, retrigger on overlap. Not in v1: probability, ratchets, per-step CV beyond pitch, pattern chaining, keyboard editing.

Name: `gseq` (4 characters, so Synthor lists it).

## Step 1: extract the clock

The clock code is about 80 lines of `AldaEngine::process`, `pulse`, `swung` and `midiClock`. It shares state with the score: alda's loop length, `restart()`, and the events a pulse skips over at the loop's end.

`ssp::engine::Clock`, JUCE-free like the engines:

- **State:** position in ticks (480 a quarter), the last pulse's grid position, the measured rate, the source (free, input, MIDI), MIDI halt, the edge detectors for reset and clock inputs, swing.
- **Per block:** `begin(source, bpm, tempo, swing)` applies a source change (the grid snap alda does now) and MIDI transport, and returns Start or Stop for the owner to act on.
- **Per sample:** `step(reset, clock)` takes the two input samples and returns a flag for a reset edge. `position()` and `swung()` give the time the owner plays events up to.
- **Loop:** the owner wraps. `wrap(length)` subtracts a length from the position and the grid. alda calls it at its score's end; gseq does not, because rows wrap on their own. The pulse that crosses alda's loop end becomes: the clock reports the target, alda plays the remaining events, then wraps.
- **MIDI input thread:** `midiClock(seconds)` and the transport atomics move as they are.

Check: `plugins/alda/tests/alda_test.cpp` covers input clock, MIDI clock with Start, Stop and Continue, the Clock control, and source changes mid-score. They must pass unchanged. A new `clock_test.cpp` in `plugins/common/tests` covers the class alone, including a position that never wraps (gseq's case). At 120 bpm (960 ticks/s), a double's step stays under 2e-6 ticks for 2,000 hours, against 0.02 ticks advanced per sample at 48 kHz. Then alda's device test, steps on clocking, runs again on the SSP.

## Open questions

1. Is duration in steps, or as a fraction of one step (gate length)? The sketch uses steps, 0.1-8.
2. Should one row drive MIDI and CV at once, or one target per row? The sketch allows both.
