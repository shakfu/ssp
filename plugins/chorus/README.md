# chorus

`chorus` is a stereo chorus whose DSP is Faust. It is the first module built on `FaustProcessor`; see [docs/dev/faust.md](../../docs/dev/faust.md). `Source/chorus.dsp` comes from sk-engines.

## Install

Copy `chrs.so` to the `plugins` folder on the SD card (`make install MOD=chrs`).

## Controls

One page: delay, depth, mix, rate, each 0..1. Faust sorts controls by label.

- **delay**: base delay, 5 to 20 ms.
- **depth**: the LFO adds 0 to 5 ms at 1.
- **mix**: 0 dry, 1 wet.
- **rate**: LFO rate, 0.05 to 5 Hz. The right LFO runs 3% faster.

## Inputs and outputs

| Inputs | Outputs |
|-|-|
| In L, In R; a CV per control: delay, depth, mix, rate | Out L, Out R |

A CV adds to its control: 5 V spans the range.

## Changing the DSP

Edit `Source/chorus.dsp`, then run `make faust-kernels` to regenerate `Source/ChorusKernel.h`. It needs `uv`, which fetches cyfaust.
