# dexed

`dexed` is a 6-operator FM synth played from MIDI. It runs the [Dexed](https://github.com/asb2m10/dexed) engine through [Synth_Dexed](https://codeberg.org/dcoredump/Synth_Dexed) and plays DX7 cartridges from the card. GPLv3; see [NOTICE.md](NOTICE.md).

Design and plan: [docs/dev/dexed.md](../../docs/dev/dexed.md).

## Install

Copy `dexd.so` to the `plugins` folder on the SD card (`make install MOD=dexd`). The module is `dexd`. Put cartridges under `dexed/` on the card's BOOT partition (`/media/BOOT/dexed`); Load opens there.

## Playing

Set MIDI IN in the module's general panel (RS + LS). Note In is on by default.

| Message | Effect |
|-|-|
| note on and off, velocity | notes |
| program change 0-31 | the cartridge's voice 1-32; 32-127 are ignored |
| pitch bend | over Bend Range |
| CC1 mod wheel, channel aftertouch | LFO pitch depth, as on the DX7 |
| CC64 sustain, CC66 sostenuto | |
| CC120 all sound off, CC123 all notes off, CC121 reset controllers | |

CC2 (breath) and CC4 (foot) reach the engine but have no target set.

Changing Voice (or a program change to another voice), Voices, Engine or Mono cuts the notes playing.

## Cartridges

Load (button 5) picks a `.syx` file: a 32-voice bulk dump of 4104 bytes, alone or inside a longer sysex file, or its 4096 bytes of data. A dump whose checksum fails still loads, as in Dexed; the status panel says so. Each value is clamped to its DX7 range.

With no cartridge, every voice is Dexed's init voice: a sine. None ship with `dexd`. The DX7 factory banks are `Original Yamaha/DX7 ROM1` to `ROM4` in [Yamaha-DX7-patch-library](https://github.com/visualizersdotnl/Yamaha-DX7-patch-library); copy the `.syx` files to `BOOT/dexed`.

The preset stores the cartridge's path and the selected voice's data. If the file has gone, the preset plays its saved voice in every slot.

## Controls

The controls change the voice as it plays; the cartridge is not modified. Offsets add to the voice's value and clamp to its range.

| Page | Encoder 1 | Encoder 2 | Encoder 3 | Encoder 4 |
|-|-|-|-|-|
| voice | Voice | Level | Transpose | Voices |
| tone | Cutoff | Resonance | Algorithm | Feedback |
| op 1-4 | Op 1 | Op 2 | Op 3 | Op 4 |
| op 5-6 lfo | Op 5 | Op 6 | LFO Rate | LFO Depth |
| play | Bend Range | Portamento | Mono | Engine |

- **Voice**: 1-32 in the cartridge. PG- and PG+ (Left and Right) step it, wrapping from 32 to 1.

- **Level**: output gain, 0-2.

- **Transpose**: semitones, -24 to +24. A held note releases at the pitch it started.

- **Voices**: polyphony, 1-16. A note past it takes the voice held longest.

- **Cutoff**, **Resonance**: Dexed's resonant low-pass filter.

- **Algorithm**: `voice` keeps the voice's; 1-32 replaces it.

- **Feedback**: offset, -7 to +7.

- **Op 1-6**: output level offset, -99 to +99.

- **LFO Rate**, **LFO Depth**: offsets to the voice's LFO speed and pitch modulation depth.

- **Bend Range**: semitones, 0-12.

- **Portamento**: time, 0 (off) to 99.

- **Engine**: `modern`, `mark I` (default) or `OPL`, Dexed's three resolutions.

The status panel shows the cartridge, the voice's number and name, notes playing, and DSP load.

## Jacks

| Jack | |
|-|-|
| Cutoff CV | adds to Cutoff; 1.0 (5 V) is the full range. Cutoff defaults to 1.0, fully open, so lower it to give positive CV room |
| Mod CV | the mod wheel, 0 to 1.0 (5 V); the higher of this and CC1 applies |
| Out L, Out R | the synth, mono, on both |

CV is read once per block.
