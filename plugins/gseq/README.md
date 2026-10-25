# gseq

`gseq` is a step sequencer shown as a grid. It has 8 rows of up to 32 steps. Each row has its own length, so rows of different lengths drift against each other (polymeter). Each step holds a note, a velocity and a duration. A row plays on a MIDI channel, on a CV voice, or both. It makes no sound itself: patch the outputs into oscillators and envelopes, or send the MIDI to a synth.

## Install

Copy `gseq.so` to `BOOT/plugins` (from source: `make install MOD=gseq`) and restart the SSP. A Launchpad is optional; see [Launchpad](#launchpad).

## First use

1. Add `gseq`.
2. Patch any `INPT` output into In 1 or In 2. gseq runs only while a jack is patched ([Using](../../docs/USING.md#a-module-runs-only-while-a-jack-is-patched)).
3. Patch `Gate 1` and `CV 1` to a voice, or choose `gseq` as a synth's MIDI IN.
4. Turn encoder 1 on a cell. The step turns on and row 1 plays it.

## Jacks

| In | Name | Does |
|-|-|-|
| 1 | `Reset` | a rising edge returns every row to its first step |
| 2 | `Clock` | clock pulses; see [Clock](#clock) |

| Voice | Gate | Pitch CV | Velocity CV |
|-|-|-|-|
| 1 | Out 1, `Gate 1` | Out 5, `CV 1` | Out 9, `Vel 1` |
| 2 | Out 2, `Gate 2` | Out 6, `CV 2` | Out 10, `Vel 2` |
| 3 | Out 3, `Gate 3` | Out 7, `CV 3` | Out 11, `Vel 3` |
| 4 | Out 4, `Gate 4` | Out 8, `CV 4` | Out 12, `Vel 4` |

- Gate: 5 V while a note sounds, at least 2 ms.

- Pitch: 1 V/octave, C4 (MIDI 60) at 0 V, within the SSP's +-5 V: notes above 120 clip, and their cells get a red frame. It holds after the gate closes.

- Velocity: 127 at 5 V, when the row's velocity CV is on; 0 V when off. `held` keeps it until the next note; `gated` only while the gate is open, 0 V otherwise. Gated velocity into an oscillator's level input (Plaits' LEVEL, with TRIG empty) gives notes their duration and their velocity from one jack.

- A step that starts while the row's last note still sounds retriggers it: the gate closes for 1 ms, and MIDI gets a note-off first.

## The grid

Each row's label shows its number, MIDI channel and CV voice (`1 ch1 cv1`), then its length and division (`16 x 1/16`). A division is the time one step lasts. A lit cell is a step that plays: brighter for higher velocity, its note name across it, and an orange bar for its duration. A white frame marks each row's playing step; the cyan frame is the cursor.

"Held" means turned while pressed.

| Control | Grid | Row page | Global page |
|-|-|-|-|
| Encoder 1 | note (held: octaves); a turn sets the step on; press toggles it | MIDI channel, off or 1-16 | tempo, 20-300 (held: by 10); press: grid or roll mode |
| Encoder 2 | velocity (held: by 10) | CV voice, off or 1-4 | swing, 50-75% |
| Encoder 3 | duration: 1/8 to 7/8 of a step, then 1, 1.5, 2, 3, 4, 6, 8, 12, 16 steps | velocity CV: off, held, gated | clock: free, analog, midi |
| Encoder 4 | row length 1-32; press: next division | division: 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 | PPQN: clock pulses per quarter note, 1-24 |

| Button | Does |
|-|-|
| Row | the row page for the cursor's row; again returns to the grid |
| Mute | mutes the cursor's row: its notes no longer start |
| Copy, Paste | copies the cursor's row; pastes its steps, length and division into the cursor's row, which keeps its MIDI channel and CV voice |
| Global | the global page; again returns to the grid |
| Clear | held: turns every step of the cursor's row off |
| Rec | records the cursor's row from a MIDI keyboard, and turns Step off; see [Recording](#recording) |
| Step | step entry from a MIDI keyboard, on or off; see [Recording](#recording) |
| ROW-, ROW+, STP-, STP+ | move the cursor; held, to the first or last row or step |

A new module's rows 1-4 play MIDI on channels 1-4 and CV voices 1-4, with velocity CV; rows 5-8 play MIDI on channels 5-8 only. Turn either off on the row page: MIDI channel or CV voice to `off`. A row with neither reads `no output`, and its lit steps are grey. A CV voice belongs to one row: the row page skips voices other rows hold. Rows may share a MIDI channel, as drum rows on channel 10 do.

A preset keeps the pattern.

## Recording

Play a MIDI keyboard into `gseq`'s MIDI IN, in the general panel (LS + RS, see [Using](../../docs/USING.md#the-general-panel)). Notes count only while Rec or Step is on.

- Rec: blinks until the cursor's row starts its next pass, then lights and records that one pass. Moving the cursor to another row while it blinks arms that row instead. Each note lands on the nearest step; steps no note lands on are cleared, so the take replaces the row. It then stops; press Rec again for another take, or while it blinks or records to stop it.
- Step: each note goes to the cursor's step, and the cursor moves on when every key is up. The grid may be playing or not.
- A note's duration is how long it was held, snapped to the nearest duration and capped at the row's length. Its velocity is kept.
- A chord spreads over rows: its lowest note on the cursor's row, the next on the row below, and so on, wrapping from row 8 to row 1. Only the cursor's row is replaced; the chord's notes overwrite their steps on the other rows.
- While recording, the keyboard sounds through each row's own routing: its MIDI channel, its CV voice, or both. A synth that also hears the keyboard directly sounds it twice.

The header shows `REC row N armed` or `recording`, or `STEP`.

## Roll mode

A piano roll of one track: pitch upward, steps to the right, a note per lit cell, chords in a column. On the Global page, press encoder 1 to switch between `grid` and `roll`; a preset keeps the mode.

- The 8 rows become the track's voices: gseq puts each note on a free row. Up to 8 notes sound at once; rows 1-4 play CV voices 1-4, so 4 of them reach the CV jacks, and every one reaches MIDI. A note on a row without a CV voice has a grey frame.
- Switching to roll gives every row row 1's MIDI channel, length, division and velocity CV. Switching back to grid keeps the rows as they are, with the notes on the rows gseq chose.
- The screen shows an octave, C4 to B4 at first; the line under the grid counts the notes above and below it.

| Control | Roll mode |
|-|-|
| ROW-, ROW+ | the cursor's pitch, a semitone (held: an octave) |
| STP-, STP+ | the cursor's step |
| Encoder 1 | moves the note at the cursor to another pitch, with the cursor; with no note there, moves the cursor. Press: adds or removes a note |
| Encoders 2, 3 | the note's velocity and duration |
| Encoder 4 | the track's length; press: next division |
| Row | the track page: MIDI channel, CV on or off (press: poly or mono), velocity CV, division |
| Mute, Clear | the whole track |
| Copy, Paste | grid mode only |
| Rec, Step | as in grid mode, for the whole track: a Rec take replaces every row, and each note, chord or overlapping, gets its own row |

Mono, for a single oscillator: on the track page, press encoder 2. Every note then goes to row 1 (CV voice 1 and MIDI), one per step: a new note in a step replaces the one there, a note runs only until the next, and keys sound one at a time. Switching mono on keeps one note per step, row 1's or else the lowest row's, and deletes the others; switching back to poly does not bring them back.

## Launchpad

A Novation Launchpad Mini MK3 or Launchpad X edits and shows the grid. Plug it into the SSP's left USB port through a powered hub ([Using](../../docs/USING.md#usb-devices)). `gseq` finds it within a second, with no setting; with several `gseq` modules, one drives it, and another takes over when it is removed. Do not choose the Launchpad as `gseq`'s MIDI IN: its pads would arrive as notes.

```
 [^][v][<][>] [Rec][Stp][Fol][  ]      top row; the 8th button does nothing
 [ 6 rows x 8 steps of the grid  ] [m]  cursor pads; side keys mute their rows
 [ .  C# D# .  F# G# A# .        ] [+]  key pads; side keys: octave up
 [ C  D  E  F  G  A  B  C        ] [-]                         and down
```

- Arrows move the window: up and down by a row, left and right by 8 steps. The window moves by itself to show the cursor when the SSP or step entry moves it.
- Session, Drums and Keys (Session, Note and Custom on the X) are Rec, Step and Follow; their printed labels do not apply. Rec also turns Step off. Follow keeps the cursor row's playing step in view.
- Steps light amber, brighter for higher velocity; each row's playing step is white; the cursor flashes.
- On the SSP's screen, a dim red frame marks the steps the Launchpad shows, in grid and roll mode.
- The X's pads send velocity: key pads play it, and Step entry and roll pads write it. The Mini's pads do not, so 100 is used.

| Mode | Cursor pad | Key pad |
|-|-|-|
| neither | moves the cursor | sounds through the cursor row's routing |
| Rec | moves the cursor; before the take starts, Rec moves to its row | sounds; records during the take |
| Step | switches the step on (the last key's note) or off; with keys held, sets their notes, a chord down the rows | sounds; held for the next cursor pad |

In Step mode the cursor stays where the pad put it; step entry from a MIDI keyboard still moves it on.

### Roll mode on the Launchpad

- All 8 pad rows are pitches, C4 to G4 at first. There are no key pads.
- Up and down move the pitches a semitone.
- A pad moves the cursor to its pitch and step, in every mode. In Step mode it also adds or removes a note.
- The side keys do nothing.

Record into the roll from a MIDI keyboard.

## Monome grid

A 16 x 8 monome grid edits and shows the pattern. Plug it into the SSP's USB hub; `gseq` finds it within a second, with no setting. With several `gseq` modules, one drives it, and another takes over when it is removed. A grid and a Launchpad can be used at once.

- The whole grid is the pattern: grid row 1 is row 1, and the 16 columns are steps 1-16 or 17-32. The page follows the cursor: move the cursor past step 16 with the SSP's arrows to see steps 17-32.
- A key moves the cursor there and switches the step on or off. A step switched on takes the last note played on a MIDI keyboard or a Launchpad key, else C4, at velocity 100.
- Brightness: off steps dim, on steps brighter for higher velocity, the playing step full. A step on in a muted row, or a row with no output, stays dim. The cursor blinks.
- Mutes, Rec, Step and paging are on the SSP; the grid has no buttons for them.
- Roll mode: the 8 rows are 8 pitches, highest at the top, and a key adds or removes the note there. The pitches follow the cursor's pitch.
- Steps past a row's length are dark, and keys there do nothing.

## Clock

| Clock | Time from |
|-|-|
| free | the tempo |
| analog | rising edges on In 2 (`Clock`), PPQN per quarter note (4 by default: 16ths) |
| midi | MIDI clock from `gseq`'s MIDI IN (general panel), 24 per quarter note; Start returns to the first steps, Stop halts and releases notes, Continue resumes |

Swing delays every second 16th.

## MIDI

`gseq` opens an ALSA MIDI port named `gseq` (a second instance `gseq 2`, and so on). To play `dexd` or another module, choose `gseq` as that module's MIDI IN in its general panel. To play external gear, choose a device as `gseq`'s own MIDI OUT. Removing the module sends All Notes Off on all 16 channels. Timing follows the audio block (2.7 ms at 128 frames).

## Not yet

- Scales (TODO.md); probability, ratchets, pattern chaining; a computer keyboard; MIDI clock to the Launchpad, so its flashing follows the tempo.

## Licence

AGPL-3.0, as the repo.
