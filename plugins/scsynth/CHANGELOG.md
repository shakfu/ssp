# Changelog

scsynth follows [semantic versioning](https://semver.org). Until 1.0.0, a minor version may change parameters, so presets saved with an earlier version can restore differently.

The version is set in `CMakeLists.txt` (`project(SCSYNTH VERSION ...)`) and shown at the top right of the screen.

## [0.1.0] - 2026-10-10

First version, as the module `scsy`. scsynth 3.14.1 and its core UGens, and sc3-plugins 3.14.1, loaded from `/media/BOOT/scsy/ugens`. Runs a `.scsyndef` chosen with Load, or a built-in drone. Eight audio inputs and outputs. `p1`..`p16` drive the def's named controls, with ranges from a sidecar `.txt` or from the defaults; the sidecar's `cv N` adds input N as CV, at audio rate. A failed load keeps the previous def running. Three examples in `examples/`: two filters and an FM voice. See [README.md](README.md).
