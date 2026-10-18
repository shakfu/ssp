# Changelog

dexed follows [semantic versioning](https://semver.org). Until 1.0.0, a minor version may change parameters, so presets saved with an earlier version can restore differently.

The version is set in `CMakeLists.txt` (`project(DEXED VERSION ...)`) and shown at the top right of the screen.

## [0.1.0] - 2026-10-18

- First version: Synth_Dexed `e3f524c`, played from MIDI, with DX7 cartridges from the card and 5 pages of performance controls. PG- and PG+ (Left and Right) step through the voices. See [README.md](README.md).

- Synth_Dexed's voice steal stamped the loop counter's voice, not the voice it started. It stole the wrong voice, and with every voice in use it wrote one entry past the voice array. Patched; see [Source/synth_dexed/README.md](Source/synth_dexed/README.md).
