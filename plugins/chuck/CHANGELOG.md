# Changelog

chuck follows [semantic versioning](https://semver.org). Until 1.0.0, a minor version may change parameters, so presets saved with an earlier version can restore differently.

The version is set in `CMakeLists.txt` (`project(CHUCK VERSION ...)`) and shown at the top right of the screen.

## [0.1.0] - 2026-10-07

First release, as the module `chuk`. ChucK 1.5.5.8, linked in. Runs a `.ck` chosen with Load, or a built-in drone. Eight audio inputs and outputs, globals `p1`..`p16`, and ChucK's own MIDI. A failed compile keeps the previous program running. A comment line `@pN label [min max [unit]] [log]` names a control and sets its range; the editor shows only declared controls. Three example programs in `examples/`. See [README.md](README.md).
