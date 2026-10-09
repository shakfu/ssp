# Changelog

edrums follows [semantic versioning](https://semver.org). Until 1.0.0, a minor version may change parameters, so presets saved with an earlier version can restore differently.

The version is set in `CMakeLists.txt` (`project(EDRUMS VERSION ...)`) and shown at the top right of the screen.

## [0.2.0] - 2026-10-08

Each track has a trigger output, a Voice switch to send only the trigger, and a Swing of 50% to 75% on its own steps. Rate replaces Div: `/8` to `/2` divides the clock as Div did, and `x2` to `x8` multiplies it over the measured clock period. A 0.1.0 preset restores every track at `x1`.

The pages after the sixth could not be reached: the editor stacked every page as a row, and the screen holds six. It now scrolls. A long Up or Down jumps between tracks, a long Right to the global pages and a long Left back.

## [0.1.0] - 2026-10-08

First release, as the module `edrm`. Four Euclidean tracks, each with hits, steps, rotation and clock division, driving a synthesized kick, snare, clap, hat or tom. Per track: pitch, decay, level, chance, mute, and four timbre controls. A clock and a reset input, a button per track, three stereo routes and an output per track. See [README.md](README.md).
