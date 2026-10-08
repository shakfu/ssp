# glitch

`glitch` is two lo-fi noise voices. Each deck runs one of 12 algorithms ported from Rob Scape's [Noisferatu](https://github.com/rob-scape/noisferatu): buffer glitches, logic noise, generative scale blips and rhythmic noise.

Ported from the `glitch` engine in sk-engines; see [docs/dev/engines.md](../../docs/dev/engines.md). `gltc` is GPLv3; see [NOTICE.md](NOTICE.md).

## Install

Copy `gltc.so` to the `plugins` folder on the SD card (`make install MOD=gltc`). The module is `gltc`: the SSP lists a module only if its id spells its name.

## Controls

| Page | Encoder 1 | Encoder 2 | Encoder 3 | Encoder 4 |
|-|-|-|-|-|
| A voice | Algo | P1 | P2 | Pitch |
| A out | Tone | Level | | |
| B voice, B out | as deck A | | | |
| mix | A/B | Route | | |

- **Algo**: the algorithm. P1 and P2 mean different things in each; the screen names them.
- **Pitch**: +/-2 octaves, for the pitched algorithms.
- **Tone**: a one-pole low-pass, 0 dark, 1 open.
- **Route**: `stereo` centres both decks; `split` puts A left, B right; `random` pans each deck at random.
- Buttons 1 and 2 refill deck A's and B's glitch buffer, for the three buffer algorithms.

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

- Up and Down step through the pages. A long Up or Down jumps to the first page of the other deck; a long Right (Prog +) to the global pages, and a long Left (Prog -) back.

## I/O

| Input | |
|-|-|
| A Pitch, B Pitch | V/oct, added to Pitch, within its +/-2 octaves |
| A P1, A P2, B P1, B P2 | added to P1 and P2; 5 V spans the range |

| Output | |
|-|-|
| Out L, Out R | the mix, through a soft limiter |
| A Out, B Out | each deck after Tone and Level, before the crossfade and route |
