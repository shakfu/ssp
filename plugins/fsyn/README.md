# fsyn

`fsyn` plays SF2 soundfonts from MIDI, in stereo, with [FluidSynth](https://github.com/FluidSynth/fluidsynth). With a General MIDI font it plays a score from `alda` on 16 channels, drums on channel 10. Licences: [NOTICE.md](NOTICE.md).

## Install

Copy `fsyn.so` to `BOOT/plugins` (from source: `make install MOD=fsyn`) and restart the SSP.

Copy `FluidR3_GM.sf2` from the release to `BOOT/fsyn` (from source: `make install-fsyn`, on a mounted card or over ssh). A new `fsyn` loads it; Load opens `BOOT/fsyn`.

Playing needs a MIDI source: `alda`, `gseq`, or a keyboard on a powered hub ([Using](../../docs/USING.md#usb-devices)).

## First use

1. Add `fsyn`. It loads `FluidR3_GM.sf2`.
2. Patch Out L and Out R to the outputs ([Using](../../docs/USING.md#a-module-runs-only-while-a-jack-is-patched)).
3. Open the general panel (LS + RS, see [Using](../../docs/USING.md#the-general-panel)) and set MIDI IN to your keyboard, `gseq` or `1:alda`.
4. Play a note.

## Playing

Set MIDI IN in the general panel: `1:alda` for `alda`. Note In is on by default, on every channel.

| Message | Effect |
|-|-|
| note on and off, velocity | notes |
| program change, CC0 and CC32 bank select | the channel's preset |
| CC7 volume, CC10 pan, CC11 expression, CC64 sustain, CC91 reverb, CC93 chorus | as FluidSynth handles them |
| pitch bend, channel and key pressure | |
| CC120 all sound off, CC121 reset controllers, CC123 all notes off | |

Channel 10 plays the font's drum kit (bank 128).

## Soundfonts

Load (button 5) picks an `.sf2` file. The old font plays until the new one has loaded. Each channel keeps its program, bank, controllers and bend; notes playing end. A font that fails to load leaves the one playing, and the status panel shows FluidSynth's reason. The preset stores the font's path.

SF3 (compressed) fonts do not load; use the SF2. An SF3 would take as much RAM as its SF2 anyway, since FluidSynth holds samples decoded.

Measured on the SSP, 2026-10-18. dsp% is one core's share for 8 channels of chords and drums, reverb and chorus on:

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

| Jack | Name | |
|-|-|-|
| In 1 | `Gain CV` | adds to Gain, 0.2 per volt, read once per block |
| Out 1, 2 | `Out L`, `Out R` | stereo |
