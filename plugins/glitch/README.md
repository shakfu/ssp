# gltc

`gltc` is two lo-fi noise voices. Each deck runs one of 12 algorithms ported from Rob Scape's [Noisferatu](https://github.com/rob-scape/noisferatu): buffer glitches, logic noise, generative scale blips and rhythmic noise.

Ported from the [`glitch` engine](https://github.com/shakfu/sk-engines/tree/main/src/engine/glitch) in [sk-engines](https://github.com/shakfu/sk-engines). `gltc` is GPL-3.0; see [NOTICE.md](NOTICE.md).

## Install

Copy `gltc.so` to `BOOT/plugins` (from source: `make install MOD=gltc`) and restart the SSP.

## First use

1. Add `gltc`.
2. Patch Out L and Out R to the outputs ([Using](../../docs/USING.md#a-module-runs-only-while-a-jack-is-patched)).
3. Both decks play `sparse`, the first algorithm.
4. Turn Algo (encoder 1 on the `A voice` page) to hear the others.

## Controls

The pages form three groups: deck A, deck B, and the global `mix` page ([Using](../../docs/USING.md#buttons)). A long `PG+` goes to `mix`; a long `PG-` returns.

| Page | Encoder 1 | Encoder 2 | Encoder 3 | Encoder 4 |
|-|-|-|-|-|
| A voice | Algo | P1 | P2 | Pitch |
| A out | Tone | Level | | |
| B voice, B out | as deck A | | | |
| mix | A/B | Route | | |

- **Algo**: the algorithm. P1 and P2 mean different things in each; the screen names them.

- **Pitch**: +/-2 octaves, for the pitched algorithms.

- **Tone**: a one-pole low-pass, 0 dark, 1 open.

- **Level**: the deck's output level, 0.8 by default.

- **A/B**: the crossfade. At 0.5 both decks play at full level; at 0 only A, at 1 only B.

- **Route**: `stereo` centres both decks; `split` puts A left, B right; `random` pans each deck at random.

- Buttons 1 and 2 refill deck A's and B's glitch buffer with a new random pattern. The buffer is 4000 samples, mostly silence with bursts of noise. Only `sparse`, `wander` and `bitmangle` play it.

| Algo | P1 | P2 |
|-|-|-|
| `sparse` | playback speed | silence |
| `wander` | playback speed | walk rate of an 80-sample window |
| `bitmangle` | playback speed | rate the address bits are corrupted |
| `tri xor` | oscillator 1 | oscillator 2 |
| `nand` | oscillator 1 | oscillator 2 |
| `fm noise` | frequency | rate the ratio is re-rolled |
| `ring mod` | oscillator 1 | oscillator 2 |
| `phrygian` | trigger rate | burst modulation |
| `penta` | clock | decay |
| `bernoulli` | chance of root over fifth | chance of minor third over seventh |
| `dust` | density | tone |
| `rhythm` | clock | division |

Pitch does not act on `dust` and `rhythm`.

## Jacks

| In | Name | Does |
|-|-|-|
| 1 | `A Pitch` | V/oct, added to Pitch, within its +/-2 octaves |
| 2, 3 | `A P1`, `A P2` | added to P1 and P2; 5 V spans the range |
| 4-6 | `B Pitch`, `B P1`, `B P2` | as 1-3, for deck B |

| Out | Name | Carries |
|-|-|-|
| 1, 2 | `Out L`, `Out R` | the mix, through a soft limiter |
| 3, 4 | `A Out`, `B Out` | each deck after Tone and Level, before the crossfade and route |
