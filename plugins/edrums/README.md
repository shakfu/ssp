# edrums

`edrums` is a four-track Euclidean drum machine with synthesized voices and trigger outputs. Each track spreads its hits as evenly as possible over its steps, and a clock steps it. Tracks have their own length and clock rate, so they can run in polymeter.

Ported from the `edrums` engine in sk-engines; see [docs/dev/engines.md](../../docs/dev/engines.md).

## Install

Copy `edrm.so` to the `plugins` folder on the SD card (`make install MOD=edrm`). The module is `edrm`: the SSP lists a module only if its id spells its name.

Patch a clock into Clock, for example from `clkd`. Each rising edge above 2 V is one pulse. Nothing plays without a clock.

## Controls

| Page | Encoder 1 | Encoder 2 | Encoder 3 | Encoder 4 |
|-|-|-|-|-|
| 1 pattern | Hits | Steps | Rotate | Rate |
| 1 voice | Model | Pitch | Decay | Level |
| 1 shape | Drive | Sweep | Tone | Bright |
| 2-4 pattern, voice, shape | as track 1 | | | |
| chance | 1 Chance | 2 Chance | 3 Chance | 4 Chance |
| swing | 1 Swing | 2 Swing | 3 Swing | 4 Swing |
| mute | 1 Mute | 2 Mute | 3 Mute | 4 Mute |
| voice | 1 Voice | 2 Voice | 3 Voice | 4 Voice |
| mix | Route | | | |

- **Hits**, **Steps**: hits spread over 1 to 16 steps. Hits above Steps fill every step.
- **Rotate**: shifts the pattern later by this many steps.
- **Rate**: `/8` to `/2` steps once every 2 to 8 clock pulses; `x2` to `x8` steps 2 to 8 times per pulse, evenly over the time between the last two pulses. The first pulse after start has no period yet, so it steps once. A pulse that comes early drops the steps still due.
- **Model**: `kick`, `snare`, `clap`, `hat` or `tom`. Pitch and Decay act within the model.
- **Drive**, **Sweep**, **Tone**, **Bright**: saturation, pitch drop, body/noise balance and noise filter cutoff. 0.5 is the model as voiced.
- **Chance**: the probability that a hit sounds.
- **Swing**: 50% is straight. Above it, every second step of the track comes late, up to 75%, where it falls halfway to the next step. It acts on the track's own steps, so it works at any Rate; steps count from the last reset. A track swings once it has measured two clock pulses.
- **Mute**: silences a track and its trigger. It keeps stepping, so it comes back in time.
- **Voice**: off, a track sends only its trigger, for a voice in another module.
- **Route**: `stereo` sums all tracks to both sides; `split` puts tracks 1 and 2 left, 3 and 4 right; `random` pans each hit at random.
- Buttons 1 to 4 fire tracks 1 to 4 at once, clock or not.
- Up and Down step through the pages. A long Up or Down jumps to the first page of the previous or next track; a long Right (Prog +) to the global pages from chance on, and a long Left (Prog -) back to where you were.

The default kit: track 1 kick on every beat, track 2 tom silent, track 3 snare on 2 and 4, track 4 hat on the off-beats, all over 16 steps.

The screen shows each track's steps. Hits are lit, the next step is outlined, and the name flashes on a hit. The name shows the rate unless it is `x1`, `trig` with the voice off, and `m` when muted.

## I/O

| Input | |
|-|-|
| Clock | a rising edge steps each track at its Rate |
| Reset | a rising edge returns every track to step 1; a clock on the same sample plays step 1 |

| Output | |
|-|-|
| Out L, Out R | the mix, through a soft limiter |
| 1 Out to 4 Out | each track after Level and Mute, before the route |
| 1 Trig to 4 Trig | 5 V for 5 ms on each hit, shortened to half the step spacing at fast rates |
