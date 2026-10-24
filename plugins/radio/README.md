# rdio

`rdio` is two independent virtual [Radio Music](https://github.com/TomWhitwell/RadioMusic) modules over one library of sound files. Each deck tunes through the stations in a bank. Every station runs on a shared free-running clock, so tuning in lands where the station would be had it played all along.

Ported from the [`radio` engine](https://github.com/shakfu/sk-engines/tree/main/src/engine/radio) in [sk-engines](https://github.com/shakfu/sk-engines).

## Install

Copy `rdio.so` to `BOOT/plugins` (from source: `make install MOD=rdio`) and restart the SSP.

Put the library in `BOOT/radio`, or choose another folder with Load. None ships.

## First use

1. Copy a few `.wav` files into `BOOT/radio/<bank>/`, where `<bank>` is any folder name.
2. Add `rdio`.
3. Patch Out L and Out R to the outputs ([Using](../../docs/USING.md#a-module-runs-only-while-a-jack-is-patched)).
4. Turn Station (encoder 1) to tune deck A through the files.

## Library

- **Root**: a folder of banks. Load (button 5) opens a browser; navigate into the root and press Load again.

- **Bank**: each subfolder of the root, in name order. A root without subfolders is a single bank.

- **Station**: each `.wav` or `.raw` file in a bank, in name order.
  - `.wav`: 16, 24 or 32-bit PCM, or 32-bit float. Any channel count, mixed to mono. Plays at its own rate.

  - `.raw`: headerless 16-bit mono, Radio Music's format. Plays at `Raw Rate` (default 44.1 kHz).

- **Name order** ignores case and compares numbers by value: `2` before `10`, `NUMBER9` before `NUMBER10`.

- **SETTINGS.TXT** in the root, from a Radio Music card, sets three controls when you choose the root with Load. A preset keeps its own values. Other keys are ignored.

  | Key | Sets |
  |-|-|
  | `crossfadeTime` or `DECLICK` | Fade ms |
  | `startPotImmediate` | Start Pot Imm |
  | `startCVImmediate` | Start CV Imm |

Stations stream from the card, so their length is unlimited.

## Controls

The pages form three groups: deck A, deck B, and the global `mix` and `settings` pages ([Using](../../docs/USING.md#buttons)). A long `PG+` goes to `mix`; a long `PG-` returns.

| Page | Encoder 1 | Encoder 2 | Encoder 3 | Encoder 4 |
|-|-|-|-|-|
| A tune | Station | Start | Speed | Level |
| A bank | Bank | Static | | |
| B tune, B bank | as deck A | | | |
| mix | A/B | Route | Raw Rate | |
| settings | Fade ms | Start Pot Imm | Start CV Imm | |

- **Station**: the tuning dial, across the bank. With Static below 0.15, the station switches once the dial has rested 180 ms. At 0.15 or above, it switches at once.

- **Start**: offset into the station, applied on the next switch or reset. With Start Pot Imm or Start CV Imm on, moving the knob or the CV jumps there at once.

- **Speed**: varispeed in octaves, -2 to +2. Radio Music has none.

- **Static**: the level of tuning noise. Between two stations on the dial, only static plays. A switch to a station starts with a 0.1 s burst of static.

- **A/B**: crossfade between the decks.

- **Fade ms**: every switch and reset crossfades from the old position to the new, 1 to 500 ms (default 15).

- **Route**: `stereo` centres both decks; `split` puts A left, B right; `random` pans each deck at random.

- Buttons 1 and 2 reset deck A and deck B: playback jumps to Start.

## Jacks

| In | Name | Does |
|-|-|-|
| 1 | `A Station` | added to the dial; 5 V sweeps the whole bank |
| 2 | `A Start` | added to Start; 5 V is the whole station |
| 3 | `A Speed` | V/oct, added to Speed |
| 4 | `A Reset` | a rising edge above 2.5 V resets the deck |
| 5-8 | `B Station`, `B Start`, `B Speed`, `B Reset` | as 1-4, for deck B |

| Out | Name | Carries |
|-|-|-|
| 1, 2 | `Out L`, `Out R` | the mix |
| 3, 4 | `A Out`, `B Out` | each deck, after Level, before the crossfade and route |
