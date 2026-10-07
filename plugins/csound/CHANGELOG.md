# Changelog

csound follows [semantic versioning](https://semver.org). Until 1.0.0, a minor version may change parameters, so presets saved with an earlier version can restore differently.

The version is set in `CMakeLists.txt` (`project(CSOUND VERSION ...)`) and shown at the top right of the screen.

## 0.1.0 - 2026-10-07

First release, as the module `csnd`. Csound 7.0.0-beta.17, linked in. Runs a `.csd` chosen with Load, or a built-in drone. Eight audio inputs and outputs, controls `p1`..`p16`, and MIDI notes through Csound's MIDI opcodes. A failed compile keeps the previous program running. A comment line `@pN label [min max [unit]] [log]` names a control and sets its range; the editor shows only declared controls. Four example orchestras in `examples/`. See [README.md](README.md).
