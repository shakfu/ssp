# gseq: grid sequencer

Status: v1 built 2026-10-22 (`plugins/gseq`; usage in its README); device test pending ([gseq-device-test.md](gseq-device-test.md)). Decided: polymeter, retrigger on overlap, velocity CV per row, duration in steps, MIDI and CV per row, and the clock extracted from alda first (see [Step 1](#step-1-extract-the-clock)).

A step sequencer shown as a grid. Each row has its own length. Each step holds a note, a velocity and a duration. A row plays to MIDI, to CV jacks, or both. The grid is edited with the encoders, without code.

## Model

- **Pattern:** up to `ROWS` rows, a tempo, a clock source, swing.
- **Row:** a length of 1-32 steps, a division (steps per quarter note), a MIDI channel, a CV voice, a mute.
  - MIDI channel: off or 1-16. Rows may share a channel, as drum rows do.
  - CV voice: off or 1-4; a voice is a gate, a pitch and a velocity jack. One row per voice: the row page refuses a voice another row holds.
  - With both set, every note goes to both. Doubling by copying a row would make every edit twice.
  - A new module: every row on its own number's MIDI channel; rows 1-4 also on voices 1-4, with velocity CV. Either can be turned off on the row page. The first version left CV off, so a new module played nothing on its jacks until each row was routed: the jacks are what a first patch uses. A row with no output reads `no output`, its steps grey.
- **Step:** on/off, note (0-127), velocity (1-127), duration in steps.
  - Duration values: 1/8 to 7/8 in eighths, then 1, 1.5, 2, 3, 4, 6, 8, 12, 16; capped at the row's length; default 1/2.
  - A gate lasts at least 2 ms, so 1/8 of a short step still opens a slow envelope.
  - Steps over fractions plus a tie flag: one encoder covers short gates and long notes, and a long note is one cell. Ties would give legato, which retrigger rules out.

Rows of different lengths drift against each other and realign after their least common multiple. A 16-step row against a 12-step row realigns every 48 steps. This is polymeter: every row's step lasts 1 / division quarter notes, whatever its length. A row's step is `floor(pos / ticksPerStep) mod length`, from the clock's position in ticks.

## Row limits

- **CV:** 4 voices. Outputs `Gate 1-4`, `CV 1-4`, then `Vel 1-4`: 12 jacks, in that order, so the first 8 match alda's and the user's preset template (gates, then CVs). sfct already has 14 outputs, so 12 fit.
- **Velocity CV:** a per-row setting, off or on. On: `Vel k` carries the step's velocity, 127 at 5 V, held until the next step. Off: `Vel k` stays at 0 V. The jacks are fixed either way, because Synthor's jacks cannot change with a setting.
- **MIDI:** the protocol imposes no row limit. Rows can share a channel: drum rows all on channel 10, one note each. The real limit is the screen. 8 rows at 46 px each fill the grid area.

So `ROWS = 8`, of which at most 4 drive CV.

## Screen

Grid area: 1600 x 370 px, above the button row (as the script Code view, 376 px).

- 32 columns of 48 px, 8 rows of 46 px. Steps past a row's length are blank.
- Cell: filled when on. Brightness = velocity. A bar from the cell's left edge = duration, running into the next cells when longer than 1 step. A note that runs past the row's end continues into the next pass, and retriggers on a step that is on. The note name fills the cell, dark or light against the fill.
- Playhead: one column per row, since rows differ in length.
- A row label on the left: length, division, MIDI channel and CV voice (`ch10`, `cv2`, `ch1 cv1`).

Editing, with the existing controls (4 encoders, 8 buttons, arrows):

| Control | Does |
|-|-|
| arrows | move the cursor between steps and rows |
| encoder 1 | note; press toggles the step |
| encoder 2 | velocity |
| encoder 3 | duration |
| encoder 4 | row length; press cycles the division |
| Row | row page: encoders set MIDI channel, CV voice, velocity CV, division |
| Global | global page: tempo, swing, clock source, PPQN (the plugin's parameters) |
| Mute, Copy, Paste, Clear | the cursor's row; Clear acts only when held |

A USB keyboard could map to the same actions through `KeyInput`, with port 7776 next after alda's 7775. That is optional for v1.

## Reuse

- **Clock:** `AldaEngine` already has free tempo, a clock input with ppqn, MIDI clock with Start/Stop/Continue, a reset input, and swing (`plugins/alda/Source/AldaEngine.h`). It moves to `plugins/common/engine` first; see Step 1.
- **MIDI out:** alda's pattern. One ALSA port per instance (`MidiOutput::createNewDevice`, `alda`, `alda 2`, ...), fed from a lock-free queue on a 1 ms thread (`midiLoop`).
- **CV:** 1 V/octave, C4 (MIDI 60) at 0 V, 0.2 per volt, as alda (`plugins/alda/README.md`). Pitch holds after the gate closes.
- **Host:** `EngineProcessor` for the worker thread and block splitting. No `ScriptEngine`: there is no program text.
- **State:** the pattern in the preset's custom XML, one attribute per row: the row's word, then its 32 steps', in hex. Any word unpacks to a valid row or step.

## Failure modes

- **Synthor skips unpatched modules.** `processBlock` runs only while a jack is patched. A MIDI-only gseq with no cables would never run. The workaround is a virtual patch from INPT, which needs documenting or an automatic fix.
- **Overlaps.** A duration past the next step's start overlaps. Decided: retrigger. On a CV row, the gate closes for 1 ms (alda's `GAP_MS`) and the new pitch and velocity apply. On MIDI, a note-off for the sounding note goes before the new note-on, also when the two notes are equal.
- **Timing.** Events quantize to the audio block: 2.67 ms at 128 frames. alda's 1 ms MIDI poll adds up to 1 ms of jitter. Both are acceptable for a step sequencer; this is an inference, not measured.
- **Edits while playing.** Built as one 32-bit atomic word per step and per row, not alda's pending and retired pointers: an edit is one store, and the audio thread reads the words as it plays, so nothing is allocated or freed. A multi-step edit (Paste, Clear) can be heard half done for a block; acceptable for a step sequencer.
- **Pitch range.** +-5 V covers MIDI 0-120 at C4 = 0 V. Notes above 120 clip on CV but not on MIDI; a red frame marks a clipped note on a row with a CV voice.
- **Layer offset.** On a row with both, CV lands within the audio block and MIDI through the 1 ms send thread: up to about 1-4 ms apart (inferred, not measured).

## Alternatives

1. **Drum grid.** Each row is one fixed note; a step is on/off plus velocity. This is the more common "simple visual grid" and needs one encoder, not three. The proposed model already includes it: a row whose steps share one note. A per-row "fixed note" flag would make that the default.
2. **Polyrhythm instead of polymeter.** Every row spans the same bar, so 12 steps are triplets against 16. That is one flag per row: division from length.
3. **No new module.** alda already drives 4 CV voices and MIDI. A grid editor that writes alda text keeps one engine. Against it: alda text has no grid, and round-tripping edits through text loses the cursor model.
4. **ttyp patterns.** Teletype has 4 patterns of 64 values with lengths. That covers note-only rows, but not velocity or duration per step.

Recommendation: the proposed model, with alternatives 1 and 2 as row flags. Both cost little and cover the common grid uses.

## Recording

Built 2026-10-22, after v1; the decisions are in `plugins/gseq/TODO.md`, usage in the README. Notes reach the engine through a queue from the MIDI thread, read at each block's start, so they quantize to the block (2.7 ms), and the block takes only the notes queued when it starts. A step written in a take does not play in that pass: the key already sounds it, through the row's routing.

## v1 scope

8 rows, 32 steps, per-row length and division, note, velocity and duration, MIDI and CV targets, velocity CV per row, shared clock, preset storage, retrigger on overlap. Not in v1: probability, ratchets, per-step CV beyond pitch, pattern chaining, keyboard editing.

Name: `gseq` (4 characters, so Synthor lists it).

## Step 1: extract the clock

The clock code is about 80 lines of `AldaEngine::process`, `pulse`, `swung` and `midiClock`. It shares state with the score: alda's loop length, `restart()`, and the events a pulse skips over at the loop's end.

Done 2026-10-22: `plugins/common/engine/Clock.h`, JUCE-free like the engines, used by alda. Its header gives the order of calls each block. The owner loops, if at all:

- `clockInput()` and `midiPulse()` return true for a pulse that moved the position; alda then checks its loop end, plays the loop's last events, `wrap()`s and restarts, as its `pulse()` did. gseq never calls `wrap()`.
- `resetInput()` returns true at a reset edge; `transport()` returns MIDI Start and Stop for the owner to restart or release.
- `transport()` also fixes the MIDI ticks the block takes. alda read the count once a block; reading it per tick let a stream of ticks hold the audio thread, which alda's ThreadSanitizer test caught as a hang.

Check: `plugins/alda/tests/alda_test.cpp` covers input clock, MIDI clock with Start, Stop and Continue, the Clock control, and source changes mid-score; it passes unchanged, also under ThreadSanitizer. `plugins/common/tests/clock_test.cpp` covers the class alone, including a position that never wraps (gseq's case). At 120 bpm (960 ticks/s), a double's step stays under 2e-6 ticks for 2,000 hours, against 0.02 ticks advanced per sample at 48 kHz. Pending: alda's device test, steps on clocking, again on the SSP.
