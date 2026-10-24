# chrs

`chrs` is a stereo chorus whose DSP is written in Faust. `Source/chorus.dsp` comes from the [`chorus` engine](https://github.com/shakfu/sk-engines/tree/main/src/engine/chorus) in [sk-engines](https://github.com/shakfu/sk-engines).

## Install

Copy `chrs.so` to `BOOT/plugins` (from source: `make install MOD=chrs`) and restart the SSP.

## First use

1. Add `chrs`.
2. Patch a source into In L and In R ([Using](../../docs/USING.md#a-module-runs-only-while-a-jack-is-patched)).
3. Patch Out L and Out R to the outputs. The defaults give a half-wet chorus.

## Controls

One page. Encoders 1-4 are delay, depth, mix and rate, each 0..1.

- **delay**: base delay, 5 to 20 ms.

- **depth**: the LFO adds 0 to 5 ms at 1.

- **mix**: 0 dry, 1 wet.

- **rate**: LFO rate, 0.05 to 5 Hz. The right LFO runs 3% faster.

## Jacks

| Jack | Name | |
|-|-|-|
| In 1, 2 | `In L`, `In R` | audio |
| In 3-6 | `delay`, `depth`, `mix`, `rate` | CV, added to its control; 5 V spans the range |
| Out 1, 2 | `Out L`, `Out R` | audio |

## Changing the DSP

Edit `Source/chorus.dsp`, then run `make faust-kernels` to regenerate `Source/ChorusKernel.h`. It needs `uv`, which fetches [cyfaust](https://github.com/shakfu/cyfaust).
