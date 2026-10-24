# edrm

`edrm` is a four-track Euclidean drum machine with synthesized voices and trigger outputs. Each track spreads its hits as evenly as possible over its steps, and a clock steps it. Tracks have their own length and clock rate, so they can run in polymeter.

Ported from the [`edrums` engine](https://github.com/shakfu/sk-engines/tree/main/src/engine/edrums) in [sk-engines](https://github.com/shakfu/sk-engines).

## Install

Copy `edrm.so` to `BOOT/plugins` (from source: `make install MOD=edrm`) and restart the SSP.

`edrm` needs a clock: a pulse or square wave from another module, such as an LFO.

## First use

1. Add `edrm`.
2. Patch a clock into In 1, `Clock` ([Using](../../docs/USING.md#a-module-runs-only-while-a-jack-is-patched)). Each rising edge above 2 V is one pulse. Nothing plays without a clock.
3. Patch Out L and Out R to the outputs.
4. The default kit plays, one step per pulse.

## Controls

The pages form five groups: tracks 1 to 4, then the global pages from `chance` on ([Using](../../docs/USING.md#buttons)). A long `PG+` goes to the global pages; a long `PG-` returns.

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

- **Mute**: silences a track and its trigger. It keeps stepping, so it comes back in time. Buttons 1 to 4 still fire a muted track's trigger, without sound.

- **Voice**: on, the track plays its drum. Off, it sends only its trigger, for a voice in another module.

- **Route**: `stereo` sums all tracks to both sides; `split` puts tracks 1 and 2 left, 3 and 4 right; `random` pans each hit at random.

- Buttons 1 to 4 fire tracks 1 to 4 at once, clock or not.

The default kit, all over 16 steps:

1. kick on every beat;
2. tom, silent;
3. snare on 2 and 4;
4. hat on the off-beats.

The screen shows each track's steps. Hits are lit and the next step is outlined. Each track's label flashes on a hit. The label is the track number and model, such as `1 kick`, then:

- the Rate, unless it is `x1`;
- `trig` in place of the model, with Voice off;
- `m` when muted.

## Jacks

| In | Name | Does |
|-|-|-|
| 1 | `Clock` | a rising edge above 2 V steps each track at its Rate |
| 2 | `Reset` | a rising edge returns every track to step 1; a clock on the same sample plays step 1 |

| Out | Name | Carries |
|-|-|-|
| 1, 2 | `Out L`, `Out R` | the mix, through a soft limiter |
| 3-6 | `1 Out` to `4 Out` | each track after Level and Mute, before the route |
| 7-10 | `1 Trig` to `4 Trig` | 5 V for 5 ms on each hit, shortened to half the step spacing at fast rates |
