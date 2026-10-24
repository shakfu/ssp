# gseq: device test

Needs `gseq.so` in `BOOT/plugins`, an oscillator and an envelope (or a scope) for the CV steps, and `dexd` or `fsyn` for MIDI. Synthor runs `gseq` only while one of its jacks is patched: patch an `INPT` output into `Reset` first.

| # | Do | Expect |
|-|-|-|
| 1 | Add `gseq`; show it | the grid: 8 rows, `1 ch1 cv1` .. `4 ch4 cv4`, `5 ch5` .. `8 ch8`, `16 x 1/16`; white playheads move across, 120 bpm |
| 2 | ROW+, STP+ move the cursor; hold each | the cyan frame moves; held, to the last row or step |
| 3 | Row 1, step 1: press encoder 1 | the cell lights, `C4` |
| 4 | Patch `Gate 1` to an envelope and `CV 1` to an oscillator | a C4 every 16 steps, with no routing set |
| 4b | Row: encoder 1 and encoder 2 to `off`; then back to ch1 and cv 1 | `1 no output`, the step grey, the C4 stops; then all back |
| 5 | Encoder 1 up an octave held; encoder 2 down; encoder 3 to `2` | `C5`; the cell darker; the orange bar covers 2 cells; the note lasts 2 steps |
| 6 | Light steps 1 and 2 with duration `2` | step 2 retriggers the envelope: a 1 ms gap in `Gate 1` |
| 7 | Encoder 4 to length 3 on row 1; row 2 on cv 2, length 4, step 1 lit | the rows drift; both play their step 1 together every 12 steps |
| 8 | Press encoder 4 | `3 x 1/8T`: row 1's steps last a triplet 8th |
| 9 | Row 2 on the row page: cv 1 | refused: `cv1 is row 1's`; the voice skips to the next free one or stays |
| 10 | Mute | row 1's notes stop; the label reads `mute` in red |
| 11 | Copy on row 1, Paste on row 3 | row 3 has row 1's steps and length, and keeps its own routing |
| 12 | Clear: press, then hold | press: `hold Clear ...`; held: row 3's steps all off |
| 13 | `dexd` with MIDI IN `gseq`; light steps on row 1 (ch 1) | `dexd` plays them; notes end at their duration |
| 14 | Global: encoder 1 tempo 90, encoder 2 swing 66 | the header shows 90 bpm; every second 16th later |
| 15 | Global clock `analog`; 16ths into `Clock` | the grid follows the pulses and halts when they stop |
| 16 | Clock `midi`, MIDI clock with Start and Stop from a sequencer | Start returns to step 1; Stop halts and releases notes |
| 17 | A trigger into `Reset` | every row returns to step 1 |
| 18 | Save the patch; reload it | the pattern, routing and clock settings are back |
| 19 | In rack, the compact view | the clock page: Tempo, Swing, Clock, PPQN |
| 20 | A MIDI keyboard into `gseq`'s MIDI IN; cursor on row 1; Rec | Rec blinks, header `REC row 1 armed`; at row 1's step 1 it lights, `recording` |
| 21 | Play a few notes during that pass | the keyboard sounds through row 1's voice and channel; the notes land on the nearest steps, the other steps of row 1 clear |
| 22 | Wait one pass | Rec goes out; row 1 plays the take; long-held notes have long bars |
| 23 | Cursor on row 7; Rec; a three-note chord in the pass | lowest note on row 7, the next on row 8, the top on row 1; rows 8 and 1 keep their other steps |
| 24 | Step; play notes, one at a time, then a chord | each at the cursor, which moves on when the keys are up; the chord across rows |
| 25 | Rec while armed; Step while recording | Rec stops the take; Step stops it and starts step entry |
| 26 | Rec on row 1 with a long row 3; while Rec blinks, ROW+ twice; then ROW- while recording | `REC row 3 armed`; the take starts at row 3's next pass; ROW- leaves it on row 3 |
| 26b | Row page, encoder 3 to `gated`; `Vel 1` into Plaits' LEVEL, TRIG empty, `CV 1` into V/OCT | `E3 vel cv gated`; each note sounds for its duration, louder for higher velocity; silent between notes and after the steps are off |

Launchpad Mini MK3, on the powered hub with the keyboard ([gseq-launchpad.md](gseq-launchpad.md)). Rows 1-4 patched to an oscillator and envelope, or `dexd` on `gseq`'s MIDI.

| # | Do | Expect |
|-|-|-|
| 27 | Add `gseq`, with the Launchpad plugged in | within a second the Launchpad shows rows 1-6, steps 1-8: white playheads move; the keyboard rows light, C brighter |
| 28 | Press a cursor pad on row 2, step 3 | the SSP's cyan frame moves there; the pad flashes; the step stays off |
| 29 | Press a key pad, then the side key + and press it again | the cursor's row sounds the note, then an octave higher; release ends each note |
| 30 | Drums (Step); press a cursor pad; again; press G, then a cursor pad | on with C4, off; then on with G; the cursor does not move on |
| 31 | In Step: hold C, E and G, press a cursor pad on row 7 | C on row 7, E on row 8, G on row 1 (when the window shows it); the notes sound while held |
| 32 | Session (Rec) on row 1; press a cursor pad on row 3 while Rec blinks | the SSP header: `REC row 3 armed`; play key pads in the take; they land on row 3 |
| 33 | Side key beside row 1 | row 1 mutes: red side key, `mute` on the SSP |
| 34 | Arrows: down twice, right three times; then move the cursor with the SSP arrows to row 1, step 1 | the window shows rows 3-8, steps 25-32; then jumps back to rows 1-6, steps 1-8 |
| 35 | Keys (Follow) with the cursor on a 32-step row | the window pages with that row's playhead |
| 36 | Unplug the Launchpad, plug it back | within a second it shows the grid again; remove `gseq`: it returns to its own Session layout |
| 36b | Launchpad X in place of the Mini; add `gseq` | the log shows `launchpad: open LPX MIDI`; the X shows the grid as in step 27; with the Mini also plugged in, the X still drives gseq |
| 36c | On the X: a key pad softly, then hard; in Step, hold a key softly and press a cursor pad on an empty step; then hold it hard and press the same pad | the notes play at low and high velocity; the step's velocity on the SSP goes low, then high, and stays on |
| 36d | Move the Launchpad window with its arrows, in grid and in roll mode, on the X and on the Mini; unplug it | a dim red frame on the SSP follows the window: 6 rows by 8 steps, then 8 pitches by 8 steps, cut at the screen's edge; it goes when unplugged. In roll, Step, pads soft and hard on the X: the notes' velocity follows |

Roll mode ([gseq-roll.md](gseq-roll.md)). Rows 1-4 into four oscillators and envelopes (or Plaits with gated `Vel`), and `dexd` on `gseq`'s MIDI.

| # | Do | Expect |
|-|-|-|
| 37 | Global page, press encoder 1 | `roll`: 12 pitch rows, C4 at the bottom; every row takes row 1's channel; the Launchpad's 8 pad rows show C4-G4, no keyboard |
| 38 | Cursor on step 1; press encoder 1 on C4, E4, G4 (ROW- between) | a chord: three cells in a column; it sounds on CV voices 1-3 and on MIDI |
| 39 | Add 5 more notes at step 1 | the 5th to 8th have grey frames (MIDI only); a 9th: `8 notes sound at this step` |
| 40 | On a note: encoder 1, encoder 2, encoder 3 | the note moves in pitch with the cursor; velocity; duration bar |
| 41 | A note of 4 steps at step 1 on C4; another C4 at step 3 | the second retriggers the first on its row: one MIDI note at a time on C4 |
| 42 | Rec; play a chord, then overlapping notes in the pass | after the pass, the take replaced the track; each note on its own row; the chord's notes all sound |
| 43 | Launchpad: Drums (Step), press pads in the window; up and down | pads add and remove notes; the window moves a semitone |
| 44 | Save the patch, reload it | still `roll`, the notes as they were |
| 45 | Global, encoder 1 again | `grid`: each row shows the notes gseq put on it |
| 46 | One oscillator on voice 1; a chord at step 1; track page, press encoder 2 | `mono`: the step keeps one note; a new note at a step replaces it and sounds on voice 1; the nanoKEY plays one note at a time |
