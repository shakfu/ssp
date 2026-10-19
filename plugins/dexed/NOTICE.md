# Licence notice

`dexd` is GPLv3, not the AGPL-3.0 of the rest of this repository.

- `Source/synth_dexed/` is a patched copy of [Synth_Dexed](https://codeberg.org/dcoredump/Synth_Dexed), the [Dexed](https://github.com/asb2m10/dexed) engine by Pascal Gauthier and H. Wirtz: GPLv3, with the msfa files (Raph Levien, Google) under Apache 2.0, in [LICENSE-msfa.txt](LICENSE-msfa.txt). See [Source/synth_dexed/README.md](Source/synth_dexed/README.md).

- `Source/Cartridge.cpp` follows Dexed's `Cartridge::unpackProgram`.

- The other files in `Source/` are original. They form a combined work with Synth_Dexed, so they are distributed under GPLv3 too.

GPLv3 and AGPL-3.0 section 13 each allow a work under one to be combined with a work under the other. The full GPLv3 text is in [LICENSE](LICENSE).
