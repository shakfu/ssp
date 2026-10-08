# pstretch

`pstretch` is two real-time PaulStretch decks. Each deck cuts its source into large overlapping windows, randomizes the phases of each window's spectrum, and overlaps them again while its read head crawls through the source. Large stretches turn the recent input into a slowly evolving drone.

Ported from the `pstretch` engine in sk-engines; see [docs/dev/engines.md](../../docs/dev/engines.md).

## Install

Copy `strc.so` to the `plugins` folder on the SD card (`make install MOD=strc`). The module is `strc`: the SSP lists a module only if its id spells its name. For the file source, put clips in `pstretch/` on the card's BOOT partition (`/media/BOOT/pstretch`), or choose another folder with Load.

## Sources

Each deck has its own source.

- **live**: the deck's input. The read head stays within the last 5.5 seconds; at large stretches it trails the input.
- **capture**: switching to capture freezes the last 5.5 seconds (less if the deck has heard less) and loops the read head through them. The input goes on being recorded, so Grab or the Gate input captures again from what came in since.
- **file**: a clip from the clip folder, read only as fast as the read head needs it. Clips loop, so any length plays indefinitely. `.wav` (16, 24 or 32-bit PCM, or 32-bit float, any channel count) plays at its own rate; `.raw` (16-bit mono) at the engine rate.

## Controls

| Page | Encoder 1 | Encoder 2 | Encoder 3 | Encoder 4 |
|-|-|-|-|-|
| A stretch | Stretch | Diffuse | Pitch | Tone |
| A source | Source | Clip | Position | Mix |
| A mod | Mod Rate | Mod Depth | Mod Shape | Mod Target |
| B stretch, B source, B mod | as deck A | | | |
| mix | A/B | Route | Window | |

- **Stretch**: 1x to 64x, exponential.
- **Diffuse**: 0 resynthesizes each window cleanly; 1 randomizes every phase, the full PaulStretch wash.
- **Pitch**: +/-1 octave. With the Pitch input, the total is limited to +/-2 octaves.
- **Tone**: a one-pole low-pass on the output, 0 dark, 1 open.
- **Mix**: dry/wet.
- **Clip**: selects a clip in the folder, in name order. A change opens the clip once it has rested 60 ms.
- **Position**: where the clip opens, 0 to 1 of its length. A move reopens the clip there once it rests.
- **Mod Rate**: 0.03 to 7.7 Hz. **Mod Depth**: 0 turns the modulation off; the LFO output keeps running.
- **Mod Shape**: `sine`, `triangle`, or `follow`, the envelope of the deck's input.
- **Mod Target**: `diffuse`, `stretch` or `tone`.
- **Route**: `stereo` centres both decks; `split` puts A left, B right; `random` pans each deck at random.
- **Window**: 4096, 8192, 16384 or 32768 samples. Larger windows smear more and resolve frequency more finely; they also respond later. A change rebuilds both decks, which restarts their sources.
- Buttons 1 and 3 toggle Freeze on deck A and B: the read head holds still and the window keeps being re-randomized. Buttons 2 and 4 are Grab: a new capture, in capture.

The screen shows each deck's source, stretch and freeze state, and the clip it plays.

- Up and Down step through the pages. A long Up or Down jumps to the first page of the other deck; a long Right (Prog +) to the global pages, and a long Left (Prog -) back.

## I/O

| Input | |
|-|-|
| A In, B In | audio, for live and capture |
| A Pitch, B Pitch | V/oct, added to Pitch |
| A Stretch, B Stretch | added to Stretch; 5 V spans the range |
| A Mix, B Mix | added to Mix; 5 V spans the range |
| A Gate, B Gate | a rising edge above 2 V captures again in capture, else toggles Freeze |
| A/B | added to A/B; 5 V spans the range |

| Output | |
|-|-|
| Out L, Out R | the mix, through a soft limiter |
| A Out, B Out | each deck after Mix and Tone, before the crossfade and route |
| A LFO, B LFO | each deck's modulation source, 0 to 5 V |
| A Gate, B Gate | a 5 ms pulse each LFO cycle |
