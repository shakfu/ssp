# fsyn

`fsyn` plays SF2 soundfonts from MIDI, in stereo, with [FluidSynth](https://github.com/FluidSynth/fluidsynth). With a General MIDI font it plays a score from `alda` on 16 channels, drums on channel 10. Licences: [NOTICE.md](NOTICE.md).

## Install

Copy `fsyn.so` to the `plugins` folder on the SD card (`make install MOD=fsyn`), and `FluidR3_GM.sf2` to `BOOT/fsyn` (`make install-fsyn`). A new `fsyn` loads `BOOT/fsyn/FluidR3_GM.sf2`; Load opens `BOOT/fsyn`.

## Playing

Set MIDI IN in the module's general panel (RS + LS): `1:alda` for `alda`. Note In is on by default, on every channel.

| Message | Effect |
|-|-|
| note on and off, velocity | notes |
| program change, CC0 and CC32 bank select | the channel's preset |
| CC7 volume, CC10 pan, CC11 expression, CC64 sustain, CC91 reverb, CC93 chorus | as FluidSynth handles them |
| pitch bend, channel and key pressure | |
| CC120 all sound off, CC121 reset controllers, CC123 all notes off | |

Channel 10 plays the font's drum kit (bank 128).

## Soundfonts

Load (button 5) picks an `.sf2` file. It loads on the worker thread while the old font plays, then replaces it. Each channel keeps its program, bank, controllers and bend; notes playing end. A font that fails to load leaves the one playing, and the status panel shows FluidSynth's reason. The preset stores the font's path.

SF3 (compressed) fonts do not load: the build leaves out libsndfile, which decodes them. FluidSynth holds a font's samples decoded in RAM, so an SF3 would take as much memory as its SF2.

Measured on the SSP with `plugins/fsyn/tests/run_on_ssp.sh`, beside Synthor, 2026-10-18. Load is from the card, cold cache; dsp% is one core's share for 8 channels of held chords plus drums, reverb and chorus on:

| Font | Size | Load | RAM | dsp% at 32 voices | at 64 voices |
|-|-|-|-|-|-|
| FluidR3_GM (ships) | 148 MB | 2.4 s | 156 MB | 8 | 14 |
| GeneralUser-GS | 32 MB | 0.6 s | 43 MB | 8 | 17 |
| TimGM6mb | 6 MB | 0.06 s | 10 MB | 8 | 14 |

Playing `alda` scores in Synthor, FluidR3_GM ran at 4-5% DSP on average, 9-10% at peak. It sounds best of the three, GeneralUser-GS next. A font swap holds both in RAM until the old one is freed. GeneralUser-GS and TimGM6mb are in [aldakit's soundfont release](https://github.com/shakfu/aldakit/releases/tag/soundfonts-v1).

## Controls

| Page | Encoder 1 | Encoder 2 | Encoder 3 | Encoder 4 |
|-|-|-|-|-|
| synth | Gain | Reverb | Chorus | Voices |

- **Gain**: FluidSynth's master gain, 0-2. 0 also ends the notes playing.
- **Reverb**: reverb level, 0-1. CC91 sets each channel's send.
- **Chorus**: chorus level, 0-1. CC93 sets each channel's send.
- **Voices**: polyphony, 1-256, 64 by default. A note past it takes a voice from one playing.

## Jacks

| Jack | |
|-|-|
| in 1, `Gain CV` | adds to Gain, 0.2 per volt, read once per block |
| out 1, `Out L`; out 2, `Out R` | stereo |
