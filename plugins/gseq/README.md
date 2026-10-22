# gseq

`gseq` is a step sequencer shown as a grid. It has 8 rows of up to 32 steps. Each row has its own length, so rows of different lengths drift against each other (polymeter). Each step holds a note, a velocity and a duration. A row plays on a MIDI channel, on a CV voice, or both. It makes no sound itself: patch the outputs into oscillators and envelopes, or send the MIDI to a synth. Design: [docs/dev/gseq.md](../../docs/dev/gseq.md).

## Install

Copy `gseq.so` to the `plugins` folder on the SD card.

## Outputs and inputs

| Voice | Gate | Pitch CV | Velocity CV |
|-|-|-|-|
| 1 | out 1, `Gate 1` | out 5, `CV 1` | out 9, `Vel 1` |
| 2 | out 2, `Gate 2` | out 6, `CV 2` | out 10, `Vel 2` |
| 3 | out 3, `Gate 3` | out 7, `CV 3` | out 11, `Vel 3` |
| 4 | out 4, `Gate 4` | out 8, `CV 4` | out 12, `Vel 4` |

- Gate: 5 V while a note sounds, at least 2 ms.

- Pitch: 1 V/octave, C4 (MIDI 60) at 0 V, within the SSP's +-5 V: notes above 120 clip, and their cells get a red frame. It holds after the gate closes.

- Velocity: 127 at 5 V, held until the next note, when the row's velocity CV is on; 0 V when off.

- A step that starts while the row's last note still sounds retriggers it: the gate closes for 1 ms, and MIDI gets a note-off first.

- In 1, `Reset`: a rising edge returns every row to its first step. In 2, `Clock`: see Clock.

## The grid

Each row's label shows its number, MIDI channel and CV voice (`1 ch1 cv1`), then its length and step (`16 x 1/16`). A lit cell is a step that plays: brighter for higher velocity, its note name across it, and an orange bar for its duration. A white frame marks each row's playing step; the cyan frame is the cursor.

| Control | Grid | Row page | Global page |
|-|-|-|-|
| Encoder 1 | note (held: octaves); a turn sets the step on; press toggles it | MIDI channel, off or 1-16 | tempo, 20-300 (held: by 10) |
| Encoder 2 | velocity (held: by 10) | CV voice, off or 1-4 | swing, 50-75% |
| Encoder 3 | duration: 1/8 to 7/8 of a step, then 1, 1.5, 2, 3, 4, 6, 8, 12, 16 steps | velocity CV, on or off | clock: free, analog, midi |
| Encoder 4 | row length, 1-32; press cycles the step | step: 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 | the clock input's pulses a quarter, 1-24 |

| Button | Does |
|-|-|
| Row | the row page for the cursor's row; again returns to the grid |
| Mute | mutes the cursor's row: its notes no longer start |
| Copy, Paste | copies the cursor's row; pastes its steps, length and step into the cursor's row, which keeps its MIDI channel and CV voice |
| Global | the global page; again returns to the grid |
| Clear | held: turns every step of the cursor's row off |
| Rec | records the cursor's row from a MIDI keyboard; see Recording |
| Step | step entry from a MIDI keyboard, on or off; see Recording |
| ROW-, ROW+, STP-, STP+ | move the cursor; held, to the first or last row or step |

A new module's rows 1-4 play MIDI on channels 1-4 and CV voices 1-4, with velocity CV; rows 5-8 play MIDI on channels 5-8 only. Turn either off on the row page: MIDI channel or CV voice to `off`. A row with neither reads `no output`, and its lit steps are grey. A CV voice belongs to one row: the row page skips voices other rows hold. Rows may share a MIDI channel, as drum rows on channel 10 do.

The patch keeps the pattern.

## Recording

Play a MIDI keyboard into `gseq`'s MIDI IN (general panel). Notes count only while Rec or Step is on.

- Rec: blinks until the cursor's row starts its next pass, then lights and records that one pass. Each note lands on the nearest step; steps no note lands on are cleared, so the take replaces the row. It then stops; press Rec again for another take, or while it blinks or records to stop it.
- Step: each note goes to the cursor's step, and the cursor moves on when every key is up. The grid may be playing or not.
- A note's duration is how long it was held, snapped to the nearest duration and capped at the row's length. Its velocity is kept.
- A chord spreads over rows: its lowest note on the cursor's row, the next on the row below, and so on, wrapping from row 8 to row 1. Only the cursor's row is replaced; the chord's notes overwrite their steps on the other rows.
- While recording, the keyboard sounds through each row's own routing: its MIDI channel, its CV voice, or both. A synth that also hears the keyboard directly sounds it twice.

The header shows `REC row N armed` or `recording`, or `STEP`.

## Clock

| Clock | Time from |
|-|-|
| free | the tempo |
| analog | rising edges on In 2 (`Clock`), PPQN per quarter note (4 by default: 16ths) |
| midi | MIDI clock from `gseq`'s MIDI IN (general panel), 24 per quarter note; Start returns to the first steps, Stop halts and releases notes, Continue resumes |

Each pulse moves the time to the next pulse's position; between pulses it runs at the rate of the last interval, but never past the next pulse. Swing delays every second 16th. The clock is alda's (`plugins/common/engine/Clock`).

## MIDI

`gseq` opens an ALSA MIDI port named `gseq` (a second instance `gseq 2`, and so on). To play `dexd` or another module, choose `gseq` as that module's MIDI IN in its general panel (RS + LS). To play external gear, choose a device as `gseq`'s own MIDI OUT. Removing the module sends All Notes Off on all 16 channels. Timing follows the audio block (2.7 ms at 128 frames).

## Use

Synthor runs a module only while at least one of its jacks is patched, even to an input module whose hardware jack is empty. Until then `gseq` stands still. For MIDI alone, patch any `INPT` output into one of `gseq`'s inputs.

## Not yet

- Scales (TODO.md); probability, ratchets, pattern chaining; a computer keyboard.

## Licence

AGPL-3.0, as the repo.
