# radio

`radio` is two independent virtual [Radio Music](https://github.com/TomWhitwell/RadioMusic) modules over one library of sound files. Each deck tunes through the stations in a bank. Every station runs on a shared free-running clock, so tuning in lands where the station would be had it played all along.

Ported from the [`radio` engine](https://github.com/shakfu/sk-engines/tree/main/src/engine/radio) in [sk-engines](https://github.com/shakfu/sk-engines); see [docs/dev/engines.md](../../docs/dev/engines.md).

## Install

Copy `rdio.so` to the `plugins` folder on the SD card (`make install MOD=rdio`). The module is `rdio`: the SSP lists a module only if its id spells its name (see [docs/dev/engines.md](../../docs/dev/engines.md)). Put the library under `radio/` on the card's BOOT partition (`/media/BOOT/radio`), or choose another folder with Load.

## Library

- **Root**: a folder of banks. Load (button 5) opens a browser; navigate into the root and press Load again.
- **Bank**: each subfolder of the root, in name order. A root without subfolders is a single bank.
- **Station**: each `.wav` or `.raw` file in a bank, in name order.
  - `.wav`: 16, 24 or 32-bit PCM, or 32-bit float. Any channel count, mixed to mono. Plays at its own rate.
  - `.raw`: headerless 16-bit mono, Radio Music's format. Plays at `Raw Rate` (default 44.1 kHz).
- **Name order** ignores case and compares numbers by value: `2` before `10`, `NUMBER9` before `NUMBER10`.
- **SETTINGS.TXT** in the root, from a Radio Music card, sets `Fade ms` (`crossfadeTime`, or `DECLICK`), `Start Pot Imm` (`startPotImmediate`) and `Start CV Imm` (`startCVImmediate`) when you choose the root with Load. A preset keeps its own values. Other keys are ignored.

Stations stream from the card, so their length is unlimited.

## Controls

| Page | Encoder 1 | Encoder 2 | Encoder 3 | Encoder 4 |
|-|-|-|-|-|
| A tune | Station | Start | Speed | Level |
| A bank | Bank | Static | | |
| B tune, B bank | as deck A | | | |
| mix | A/B | Route | Raw Rate | |
| settings | Fade ms | Start Pot Imm | Start CV Imm | |

- **Station**: the tuning dial, across the bank. A clean move switches once the dial has rested 180 ms. With Static at 0.15 or above, it switches at once.
- **Start**: offset into the station, applied on the next switch or reset. With Start Pot Imm or Start CV Imm on, moving the knob or the CV jumps there at once.
- **Speed**: varispeed in octaves, -2 to +2. Radio Music has none.
- **Static**: tuning noise between stations, and its level on a switch.
- **A/B**: crossfade between the decks.
- **Fade ms**: every switch and reset crossfades from the old position to the new, 1 to 500 ms (default 15).
- **Route**: `stereo` centres both decks; `split` puts A left, B right; `random` pans each deck at random.
- Buttons 1 and 2 reset deck A and deck B: playback jumps to Start.

- Up and Down step through the pages. A long Up or Down jumps to the first page of the other deck; a long Right (Prog +) to the global pages, and a long Left (Prog -) back.

## I/O

| Input | |
|-|-|
| A Station, B Station | added to the dial; 5 V sweeps the whole bank |
| A Start, B Start | added to Start; 5 V is the whole station |
| A Speed, B Speed | V/oct, added to Speed |
| A Reset, B Reset | a rising edge above 2.5 V resets the deck |

| Output | |
|-|-|
| Out L, Out R | the mix |
| A Out, B Out | each deck, after Level, before the crossfade and route |
