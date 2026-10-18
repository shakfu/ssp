# Licence notice

`fsyn` is AGPL-3.0, as the rest of this repository.

- It links [FluidSynth](https://github.com/FluidSynth/fluidsynth) 2.6.1 statically: LGPL-2.1-or-later, in [LICENSE-fluidsynth.txt](LICENSE-fluidsynth.txt). `scripts/build_fluidsynth.sh` fetches and builds it unchanged; with this repository's sources, that relinks `fsyn` against another FluidSynth, as LGPL-2.1 section 6 requires.
- `FluidR3_GM.sf2`, the soundfont it ships, is Frank Wen's General MIDI set: MIT, as its own INFO chunk states, in [LICENSE-FluidR3_GM.txt](LICENSE-FluidR3_GM.txt). The copy is [aldakit's](https://github.com/shakfu/aldakit/releases/tag/soundfonts-v1), unmodified, of Debian's `fluid-soundfont-gm`.
