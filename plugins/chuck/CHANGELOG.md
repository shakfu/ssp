# Changelog

chuck follows [semantic versioning](https://semver.org). Until 1.0.0, a minor version may change parameters, so presets saved with an earlier version can restore differently.

The version is set in `CMakeLists.txt` (`project(CHUCK VERSION ...)`) and shown at the top right of the screen.

## [0.1.0] - 2026-10-10

First release, as the module `chuk`. ChucK 1.5.5.8, linked in. Runs a `.ck` chosen with Load, or a built-in drone. Eight audio inputs and outputs, globals `p1`..`p16`, and MIDI notes. A failed compile keeps the previous program running. A comment line `@pN label [min max [unit]] [log]` names a control and sets its range; the editor shows only declared controls. Four example programs in `examples/`. See [README.md](README.md).

`cv N` at the end of an `@pN` line adds input N to the control as CV, every 10 ms: an octave per volt on a `log` range, else a tenth of the range per volt. See [README.md](README.md#labels-and-ranges).

MIDI notes from the general panel's input reach the program as the globals `midiNotes`, `midiCount` and `midiEvent`, not through `MidiIn`, which opens an ALSA device itself and so ignores the panel. `examples/midi.ck` reads them. See [README.md](README.md#midi).
