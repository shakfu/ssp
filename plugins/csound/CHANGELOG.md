# Changelog

csound follows [semantic versioning](https://semver.org). Until 1.0.0, a minor version may change parameters, so presets saved with an earlier version can restore differently.

The version is set in `CMakeLists.txt` (`project(CSOUND VERSION ...)`) and shown at the top right of the screen.

## [0.1.0] - 2026-10-10

First release, as the module `csnd`. Csound 7.0.0-beta.17, linked in. Runs a `.csd` chosen with Load, or a built-in drone. Eight audio inputs and outputs, controls `p1`..`p16`, and MIDI notes through Csound's MIDI opcodes. A failed compile keeps the previous program running. A comment line `@pN label [min max [unit]] [log]` names a control and sets its range; the editor shows only declared controls. Five example orchestras in `examples/`. The status panel counts MIDI notes. See [README.md](README.md).

`cv N` at the end of an `@pN` line adds input N to the control as CV, once per block: an octave per volt on a `log` range, else a tenth of the range per volt. See [README.md](README.md#labels-and-ranges).
