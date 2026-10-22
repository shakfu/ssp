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
