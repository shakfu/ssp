# Changelog

gseq follows [semantic versioning](https://semver.org). The version is set in `CMakeLists.txt` (`project(GSEQ VERSION ...)`).

## [0.1.0] - 2026-10-24

First version. A grid of 8 rows of up to 32 steps, each row with its own length and step, so rows drift (polymeter). Each step holds a note, a velocity and a duration; each row plays on a MIDI channel, a CV voice (gate, pitch and optional velocity), or both. Retrigger on overlap; a 2 ms minimum gate. Velocity CV per row is off, held until the next note, or gated: 0 V while the gate is closed, so one jack carries a note's duration and velocity into a level input such as Plaits' LEVEL. Free tempo, clock input or MIDI clock, from alda's clock. Recording from a MIDI keyboard: Rec (button 7) replaces the cursor's row with its next pass, each note on the nearest step, and follows the cursor to another row until the pass starts; Step (button 8) writes each note at the cursor. Durations come from how long notes are held; chords spread over rows. Rows 1-4 start on MIDI channels 1-4 and CV voices 1-4 with velocity CV, rows 5-8 on MIDI only; a row with no output shows its steps grey.

Roll mode, a piano roll of one track, switched on the Global page: the 8 rows become its voices and gseq places each note, a chord across several, on a free row; rows 1-4 keep CV voices 1-4. A mono track, for a single oscillator, keeps one note per step on row 1: a new note replaces the step's, where poly would stack it on a voice nobody patched. A view over the same pattern, not a second one, so playback, recording and presets need no second path.

Launchpad Mini MK3 or Launchpad X: the top 6 pad rows edit a window of the grid, the bottom 2 are a keyboard; side keys mute rows and set the octave; the top row moves the window and holds Rec, Step and Follow. Found by name and reconnected after replugging, with no setting. The X's pads play and write their press's velocity; the Mini's play 100. The SSP's screen frames the Launchpad's window in dim red.

The pattern is a word per step and per row, read and written atomically from any thread, over a pending and retired pattern as alda swaps scores: an edit is one store, and the audio thread neither waits nor frees. A Paste or Clear can be heard half done for one block.
