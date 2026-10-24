# strc

`strc` is two real-time PaulStretch decks. Each deck cuts its source into large overlapping windows, randomizes the phases of each window's spectrum, and adds the windows back together. Its read position moves through the source at 1/Stretch of normal speed. Large stretches turn the recent input into a slowly changing drone.

Ported from the [`pstretch` engine](https://github.com/shakfu/sk-engines/tree/main/src/engine/pstretch) in [sk-engines](https://github.com/shakfu/sk-engines).

## Install

Copy `strc.so` to `BOOT/plugins` (from source: `make install MOD=strc`) and restart the SSP.

For the file source, put clips in `BOOT/pstretch`, or choose another folder with Load. None ship.

## First use

1. Add `strc`.
2. Patch audio into In 1, `A In` ([Using](../../docs/USING.md#a-module-runs-only-while-a-jack-is-patched)).
3. Patch Out L and Out R to the outputs.
4. Deck A stretches the live input 8x, fully wet.

## Sources

Each deck has its own source.

- **live**: the deck's input. The read position stays within the last 5.5 s; at large stretches it falls behind the input.

- **capture**: switching to capture keeps a fixed copy of the recent input and loops the read position through it. The copy is 5.5 s less two windows: 5.1 s at the default Window, 4.1 s at the largest, or less if the deck has heard less. Recording goes on, so Grab or the Gate input takes a new copy from the latest input.

- **file**: a clip from the clip folder, streamed from the card. Clips loop, so any length plays indefinitely. `.wav` (16, 24 or 32-bit PCM, or 32-bit float, any channel count) plays at its own rate; `.raw` (16-bit mono) at the engine rate.

## Controls

The pages form three groups: deck A, deck B, and the global `mix` page ([Using](../../docs/USING.md#buttons)). A long `PG+` goes to `mix`; a long `PG-` returns.

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

- **Window**: 4096, 8192 (the default), 16384 or 32768 samples. Larger windows smear more and resolve frequency more finely; they also respond later. A change rebuilds both decks, which restarts their sources.

- Buttons 1 and 3 toggle Freeze on deck A and B. The read position stops, and the window's phases keep being re-randomized.

- Buttons 2 and 4 are Grab on deck A and B. With Source at `capture`, Grab takes a new copy of the recent input. Otherwise it does nothing.

The screen shows each deck's source, stretch and freeze state, and the clip it plays.

## Jacks

| In | Name | Does |
|-|-|-|
| 1 | `A In` | audio, for live and capture |
| 2 | `A Pitch` | V/oct, added to Pitch |
| 3 | `A Stretch` | added to Stretch; 5 V spans the range |
| 4 | `A Mix` | added to Mix; 5 V spans the range |
| 5 | `A Gate` | a rising edge above 2 V is Grab in capture; otherwise it toggles Freeze |
| 6-10 | `B In`, `B Pitch`, `B Stretch`, `B Mix`, `B Gate` | as 1-5, for deck B |
| 11 | `A/B` | added to A/B; 5 V spans the range |

| Out | Name | Carries |
|-|-|-|
| 1, 2 | `Out L`, `Out R` | the mix, through a soft limiter |
| 3, 4 | `A Out`, `B Out` | each deck after Mix and Tone, before the crossfade and route |
| 5, 6 | `A LFO`, `B LFO` | each deck's modulation source, 0 to 5 V |
| 7, 8 | `A Gate`, `B Gate` | a 5 ms pulse each LFO cycle; none when Mod Shape is `follow` |
