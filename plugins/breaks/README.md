# brks

`brks` runs the breakbeat slicer firmware of Zack Schollz's fantastic `_core` firmware on the SSP. It plays sliced breaks, sequences their slices, and applies 16 effects at random. The firmware is [schollz/_core](https://github.com/schollz/_core) v8.1.0 (`e702abc`). It is built unchanged through `_core`'s desktop engine, the one its VCV Rack module uses. GPL-3.0; see [NOTICE.md](NOTICE.md).

Version 0.1.0 covers audio, CV and the panel's controls. It does not cover the LED ring, MIDI, the Grimoire editor or the Mode-hold functions; see [TODO.md](TODO.md).

## Install

1. Copy `brks.so` to the `plugins` folder on the SD card: `make install MOD=brks`.
2. Copy sample banks to `BOOT/brks/default`: `make install-brks`.

`make install-brks` downloads `_core`'s default sample banks: 96 samples in 6 banks, 127 MB. These are the banks that `_core`'s sample manager installs as its default samples. The ZIP is checked against the SHA-256 that `_core` pins. The script copies the bank folders and `settings` (77 MB) and leaves out the editor project.

A new `brks` loads `BOOT/brks/default`.

## Samples

`brks` reads card folders as `_core`'s sample manager writes them:

```
my-card/
  bank1/0.0.wav, 0.0.wav.info, 1.0.wav, ...
  ...
  bank16/
  settings/          optional: CV polarity, clock behaviour, effect banks, start tempo
```

To make your own:

1. Prepare the samples in the [_core sample manager](https://shop.infinitedigits.co/guides/sample-manager/) (macOS, Windows, Linux) or the [web tool](https://ezeptocore.com/). They slice, set the tempo and find the transients.
2. Copy the project folder to `BOOT/brks/<name>` on the card. The hidden `.core-manager` folder is not needed.
3. Press Load (button 5) and choose any of:
   - the folder;
   - a bank folder in it;
   - a file in a bank folder;
   - `BOOT/brks` itself, which loads the first card in it by name.

Accepted samples:

- PCM16, mono or stereo, at 44.1 or 88.2 kHz;
- 16 banks of 16 slots, up to 255 slices each.

An entry that fails to load is skipped. The status panel shows the first reason. A folder that holds no card leaves the current card playing.

A load reads every bank's metadata, then the current bank's audio into RAM: 9.5 MB for bank 1 of the default set. Changing bank loads the next one on a worker thread. The old bank plays until the new one is ready, and the new bank starts at the sample's loop boundary: up to 5.6 s for a 4-bar loop at 170 BPM.

The preset stores the card's path, the bank, the tempo and the trigger mode, besides the parameters. Reloading a preset reloads the card from its folder.

## Controls

| Page | Encoder 1 | Encoder 2 | Encoder 3 | Encoder 4 |
|-|-|-|-|-|
| play | Break | Grimoire | Amen | Random |
| sound | Sample | Volume | Filter | Gating |
| clock | Tempo | Clock Out | | |

The **play** page holds the effects and the sequencer, each as a pair:

- **Break**: how often the effects Grimoire allows play. It rises with the cube of the knob. Below about a quarter, no effect plays.
- **Grimoire**: one of 7 effect banks, from the card's `settings/grimoire`. It selects which effects Break plays, so it does nothing with Break below a quarter. The status panel lists the bank's effects, and the ones playing.
- **Amen**: the slice sequencer. Fully left plays straight through. Positions between play sequences of 1 to 32 slices. Fully right plays random slices and changes them every two bars.
- **Random**: the jump probability. Left of centre, jumps continue from where they land. Right of centre, they return to the beat. With Amen CV patched, Amen and Random set the start and end of the slice range the CV covers.

The **sound** page holds Sample and the functions the hardware reaches with Tap held:

- **Sample**: the sample in the bank.
- **Volume**: up to the middle, the volume. Past it, saturation, then fuzz. A new instance keeps the firmware's start-up volume (234 of 244, with saturation) until Volume moves.
- **Filter**: low-pass left of centre, high-pass right of it, none in the middle.
- **Gating**: slice gating, off in the bottom eighth.

The **clock** page:

- **Tempo**: 30-300 BPM, as Tap sets it. Until it is turned, the card's tempo plays. It follows the tempo playing: from Tap, from In 1, and from the card. A clock at In 1 sets the tempo instead; Tempo then shows it. To clock at another rate, divide or multiply the clock before In 1.
- **Clock Out**: Clock Out's rate, from 8 pulses a beat to one every 2 bars, as the hardware's Mult button sets it. The default, 2 a beat, is the rate In 1 expects.

| Button | Press |
|-|-|
| 1 Tap | tap tempo; starts playback when stopped |
| 2 Mode | next trigger mode: kick, snare, hihat, random |
| 3 Play | stops, or starts when stopped (the hardware's Tap + Mult) |
| 5 Load | the file browser |
| Left / Right | the previous / next bank with samples |

Left and Right steps add up until the loop's end, so Right twice moves two banks. A step back to the bank playing cancels the change. The bank wraps at both ends.

Sample CV selects the bank, not the sample, when the card's `settings/sample_cv_mapping` says `bank`, as the default card's does.

The hardware's Mult and Bank buttons are Clock Out and Left/Right. The firmware's other button combinations are not reachable in 0.1.0. Two of them are reboot and calibration.

## Jacks

| In | | Out | |
|-|-|-|-|
| 1 | Clock: 2 pulses a beat, high above 1 V | 1 | Out L |
| 2 | Tap, as button 1 | 2 | Out R |
| 3 | Mode, as button 2 | 3 | Trig: the transient trigger the mode selects |
| 4 | Mult: each trigger moves Clock Out one step faster | 4 | Clock Out |
| 5 | Bank: each trigger steps to the next bank, as Right | | |
| 6 | Play, as button 3 | | |
| 7, 8 | unused | | |
| 9 | Amen CV | | |
| 10 | Break CV | | |
| 11 | Sample CV | | |

Inputs 1-8 are triggers and the CVs start at input 9, the layout the repository's presets use. Notes:

- Trigger inputs: a gate is high above 1 V and low below 0.25 V. A press is held for at least 5 ms, since the firmware reads its buttons once a millisecond.
- In 4 and 5 act on the rising edge, however long the gate. Clock Out keeps its value after In 4 changes the rate; the status panel shows the rate playing.
- CV inputs take -5 to +5 V. Each has the polarity the card's `settings` give it, bipolar by default. A patched CV replaces its knob, as on the hardware.
- Out L and Out R are at +/-5 V full scale. Trig and Clock Out are 0 or 5 V.

## Sound and timing

The firmware runs at its own 44.1 kHz, in 441-frame blocks. Its output is resampled to the SSP's 48 kHz by 4-point Hermite interpolation. Clock and button inputs reach it with 44.1 kHz resolution.

Measured on the SSP beside Synthor, 2026-10-19, with `tests/run_on_ssp.sh`:

| | Mean | p99 | Max |
|-|-|-|-|
| a 128-sample block, Break at 1 | 47-66 us (1.8-2.5%) | 130-230 us | 362 us at FIFO priority |

At normal priority, about 9 of 7500 blocks took 3.7 ms. The test process was preempted beside Synthor; at FIFO priority, as an audio thread runs, none exceeded 1 ms. RAM per instance: 177 KB allocated at creation, plus the bank in memory.
