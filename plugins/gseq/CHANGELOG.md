# Changelog

gseq follows [semantic versioning](https://semver.org). The version is set in `CMakeLists.txt` (`project(GSEQ VERSION ...)`).

## [Unreleased]

First version. A grid of 8 rows of up to 32 steps, each row with its own length and step, so rows drift (polymeter). Each step holds a note, a velocity and a duration; each row plays on a MIDI channel, a CV voice (gate, pitch and optional velocity), or both. Retrigger on overlap; a 2 ms minimum gate. Free tempo, clock input or MIDI clock, from alda's clock. Recording from a MIDI keyboard: Rec (button 7) replaces the cursor's row with its next pass, each note on the nearest step; Step (button 8) writes each note at the cursor. Durations come from how long notes are held; chords spread over rows. Rows 1-4 start on MIDI channels 1-4 and CV voices 1-4 with velocity CV, rows 5-8 on MIDI only; a row with no output shows its steps grey.

The pattern is a word per step and per row, read and written atomically from any thread, over a pending and retired pattern as alda swaps scores: an edit is one store, and the audio thread neither waits nor frees. A Paste or Clear can be heard half done for one block.
