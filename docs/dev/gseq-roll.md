# gseq: piano roll mode

Status: steps 1-3 built 2026-10-23; on the SSP the roll works (user, 2026-10-23); the rest of the device test (step 4) pending. Decided: a roll view over the existing 8 rows (option A below), chords, a chromatic pitch axis, one switch for the whole pattern on the global page, saved in the preset. On the Launchpad, all 8 pad rows are pitches, with no keyboard, and the side keys are unused.

In roll mode, gseq's grid shows one track: pitch upward, steps to the right, a lit cell per note. A column with several lit cells is a chord. Grid mode, with a voice per row, stays the default.

## Model

The engine's pattern stays as it is: 8 rows of up to 32 steps, one note per step and row ([gseq.md](gseq.md)). In roll mode the 8 rows are the track's **voices**, a pool that its notes are placed into.

- All 8 rows share one MIDI channel, one length and one step division. Polymeter does not apply.
- Rows 1-4 play CV voices 1-4; rows 5-8 play MIDI only. So up to 4 simultaneous notes reach the CV jacks, and up to 8 reach MIDI.
- A **note** is a step that is on: its row is a voice, its step a time, its note number the cell's pitch.
- A cell (step `s`, pitch `p`) is lit when any row has step `s` on with note `p`.

Playback is unchanged: the engine plays rows as it does in grid mode. Only editing, drawing and recording know the mode.

## Placing notes

Adding a note at step `s`, pitch `p`, with duration `d`, picks a row in this order:

1. A row with a note of pitch `p` sounding at `s` (started earlier, still within its duration): the new note replaces that one's continuation, as a retrigger on that row. Two rows on one MIDI channel must not sound the same pitch at once: a synth would end both at the first note-off.
2. The first row, from row 1, whose step `s` is off and that no earlier note still covers at `s`, and whose next note starts after `s + d`. Rows 1-4 come first, so CV gets the first 4 voices.
3. The first row whose step `s` is off and that no earlier note covers. The new note then ends at that row's next note (a retrigger).
4. None: all 8 rows sound at `s`. The note is refused, and the screen says so.

"Covers" wraps: a note near the end of the track can cover the first steps. Durations already cap at the track's length.

Removing a note switches its step off in its row. Changing a note's duration re-places it when the longer note would overlap the next note in its row: to a row sounding its pitch (rule 1), else a row free for the whole duration (rule 2). With neither, it stays and the next note retriggers it.

The allocator is in the engine (`GseqEngine::addNote`, `removeNote`), so the editor, the Launchpad and recording share it. The editor and the Launchpad call it on their threads; recording calls it on the audio thread. Two placements at once can pick the same row. That needs two edits within one audio block, as with Paste today; the later write wins.

## Mono

Added 2026-10-23, for one oscillator on CV voice 1. In poly, a second note in a step goes to row 2, CV voice 2: with only voice 1 patched, the older note plays on and the new one is silent. A mono track (row 1's `mono` bit, copied to every row) places every note on row 1, one per step:

- A new note in a step replaces the one there: from the editor, the Launchpad, step entry, or a Rec take, where the last key of a chord wins.
- A note runs until the next on row 1, which retriggers it, as on a mono synth; a longer duration does not move it.
- Keys sound on row 1, one at a time.

The switch is encoder 2's press on the track page. Switching on keeps one note per step, row 1's or else the lowest row's, and deletes the others. Keeping them on rows 2-8, unplayed, would let poly bring the chords back, but the screen would show notes that do not sound.

## Switching modes

A `Mode` parameter, `grid` or `roll`, saved in the preset with the other parameters.

- Into roll: every row takes row 1's MIDI channel, length, division and velocity CV setting. Rows 1-4 take CV voices 1-4, rows 5-8 none. Mute is cleared. The steps are kept, so a grid pattern shows as a roll of its notes, possibly with clashes that the next edit leaves alone.
- Back to grid: nothing changes; each row shows the notes placed in it.

## Screen

The grid area is 336 px high. In roll mode it shows 12 pitch rows of 28 px: one octave, C4 to B4 at first, labelled with note names on the left; C rows are a shade lighter. The line under the grid counts the notes above and below the view. 32 step columns, as in grid mode.

- A note: a cell from its step, with an orange bar for its duration, brighter for higher velocity, as in grid mode.
- Notes on rows 5-8 (MIDI only) have a grey frame: they do not reach the CV jacks.
- One playhead column for the track.
- The cursor is a pitch and a step: the engine's `cursorPitch` and the cursor's column. The cursor's row stays a row index, so code that reads it as one stays in range.

| Control | Grid page, roll mode |
|-|-|
| ROW-, ROW+ | the cursor's pitch, a semitone; held, an octave. The view scrolls to show it |
| STP-, STP+ | the cursor's step |
| Encoder 1 | moves the note under the cursor up or down in pitch, with the cursor; press: adds or removes a note at the cursor |
| Encoder 2 | velocity of the note under the cursor (held: by 10) |
| Encoder 3 | duration of the note under the cursor |
| Encoder 4 | the track's length; press: its division |

| Button | Roll mode |
|-|-|
| Row | the track page: encoder 1 MIDI channel, 2 CV on or off (voices 1-4 or none), 3 velocity CV, 4 division |
| Mute | mutes the track |
| Copy, Paste | not in roll mode (a message says so) |
| Clear | held: removes every note |
| Rec, Step | see Recording |

Global page: encoder 1's press switches grid and roll; the legend shows the mode.

## Recording

Roll mode records the whole track, not the cursor's row.

- Rec: arms until the track's next pass, then records one pass. Every row's steps in the pass are replaced. Each note goes to the allocator at its nearest step, with its held duration. Chords need no spreading: the allocator gives each note its own row.
- Step entry from a MIDI keyboard: each note goes to the allocator at the cursor's step. The cursor moves on a step when every key is up, as in grid mode.
- Echo: a key sounds through the row the allocator would give it.

## Launchpad

In roll mode the whole 8 x 8 is the window, with no keyboard rows ([Launchpad layout](gseq-launchpad.md) for grid mode):

| Control | Roll mode |
|-|-|
| pads | 8 pitch rows by 8 steps, C4 to G4 at first. In Step mode a press adds or removes a note, a new one at the pad's velocity on the X (100 on the Mini); otherwise it moves the cursor |
| up, down | scroll the window a semitone |
| left, right | 8 steps |
| side keys | unused, the octave keys too |

A key pad held when roll mode comes on still ends its note on release. Recording from the Launchpad needs grid mode or a MIDI keyboard.

The window follows the cursor's pitch as it follows the cursor's row in grid mode.

## Failure modes

- **More than 4 simultaneous notes on CV.** Notes 5-8 at a step play on MIDI only; their frame shows it. With MIDI off, they are silent.
- **More than 8.** Refused, with a message.
- **Same pitch overlapping.** Placed on one row by rule 1, so MIDI never holds one pitch twice on one channel.
- **A grid pattern switched to roll.** Its notes keep their rows; two rows may sound the same pitch at once until edited.
- **Concurrent placement.** Two placements in one block can take the same row; the later wins.

## Alternatives

1. **A separate roll pattern in the engine (option B).** A 128 x 32 bitmap with a velocity and duration per note. No limit per step, but a second storage format and playback path, recording redone, and CV still limited to 4 voices.
2. **A pitch axis from a scale.** Only the scale's notes as rows: 8 rows span more than an octave. Needs the scale per track from the Scales TODO; chromatic first, decided.
3. **Roll per row group.** Some rows a roll, others grid voices. More flexible; more state and screen. Not now.

## Launchpad decisions

1. **Side keys beside the window:** unused in v1. Alternatives were auditioning the row's pitch, or muting the track on one key.
2. **Key pads:** first kept as a keyboard, 6 pitch rows in the window. Changed after the device test (user, 2026-10-23): a piano roll wants the whole 8 x 8 as pitches, so the keyboard rows and the octave keys go in roll mode.

## Steps

1. Done 2026-10-23: `GseqEngine::setRoll`, `findNote`, `addNote`, `removeNote`, `setNoteDuration`, `cursorPitch`; roll recording (every row's take, one written mask per row), step entry and key echo on the first silent row. Tests in `gseq_test.cpp`: `testRollMode`, `testRollPlace` (the four rules, wrap, refusal, re-placing), `testRollRecord` (a chord and legato), `testRollStepEntry`, `testRollEcho`; roll and placement in `testNoteOffs` and the ThreadSanitizer test. Nothing calls `setRoll` yet: the `Mode` parameter is step 2.
2. Done 2026-10-23: the `Mode` parameter (last in the layout, since MIDI automation keys parameters by index), set from the Global page and on preset load as well as each block, so a module Synthor is not running switches at once; `PluginEditor::drawRoll` and the roll controls; the track page. `test_gseq_roll` in `test_engine_plugins.py`: a preset in roll mode, its rows, and the drawn roll.
3. Done 2026-10-23: `LaunchpadView::renderRoll` and `rollPad`; all 8 pad rows from C4. `testRoll` and `testRollHeldKey` in `launchpad_view_test.cpp`.
4. Device test: `gseq-device-test.md` steps 37-45.
