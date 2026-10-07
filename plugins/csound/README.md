# csound

`csound` runs a [Csound 7](https://csound.com) orchestra (`.csd`) on the SSP. The orchestra gets eight audio inputs and outputs, sixteen controls, and MIDI.

Ported from the `csound` engine in sk-engines; see [docs/dev/engines.md](../../docs/dev/engines.md).

## Install

Copy `csnd.so` to the `plugins` folder on the SD card. The module is `csnd`: the SSP lists a module only if its id spells its name (see [docs/dev/engines.md](../../docs/dev/engines.md)). Csound is linked in; nothing else is needed. Put orchestras anywhere on the card; the browser opens at `/media/BOOT/csound`.

## Use

Load (button 5) opens a browser. Choose a `.csd` and press Load again. The screen shows the file, and the compile error if there is one. A program that fails to compile leaves the previous one running. With no program, a built-in drone plays: pitch, level and cutoff. MIDI notes play it too.

The preset stores the file's path, not its contents.

## The orchestra

| | |
|-|-|
| controls | `chnget "p1"` .. `chnget "p16"`; see Labels and ranges |
| audio in | `inch 1` .. `inch 8`, or `ins`; `nchnls_i` sets how many |
| audio out | `outch 1` .. `outch 8`, or `outs`; `nchnls` sets how many |
| MIDI | Csound's MIDI opcodes: `massign`, `notnum`, `veloc`, `cpsmidi`, `madsr`, ... |
| files | relative paths (GEN01, `diskin`, `#include`) resolve next to the `.csd` |

- The SSP sets `sr`; the orchestra's own `sr` is ignored.
- `ksmps` is the orchestra's. Output is one k-cycle late.
- Signals are scaled by `0dbfs`: a full-scale SSP signal (1.0) reads as `0dbfs`.
- MIDI comes from the device chosen in the general panel (RS + LS). Notes arrive on channel 1. Csound starts `instr n` for MIDI channel `n` unless told otherwise, so an orchestra that ignores MIDI needs `massign 0, 0`.

## Labels and ranges

A comment line names a control and gives its range; the screen then shows the name, the value in that range and the unit, and the program receives the value in that range:

```
; @p1 cutoff 20 20000 Hz log
; @p2 mix
```

The form is `@pN label [min max [unit]] [log]`, on a line that starts with `;`. The label is one word. Without a range a control is 0 to 1. `log` maps the encoder logarithmically and needs a positive range. The encoder pages show only the declared controls, four to a page, in order; a program that declares none shows all sixteen as `P1`..`P16`. Every control still exists for presets, MIDI learn and rack, and the parameter ids stay `p1`..`p16`; a preset stores the encoder position, so it restores the same position in a program with a different range.

## Examples

`examples/` holds orchestras to copy to the card's `csound/` folder. `make test` compiles and runs each one.

| File | What | Controls |
|-|-|-|
| `filter.csd` | stereo ladder filter on inputs 1-2; input 3 is cutoff CV, 1 V/oct | cutoff, resonance, drive, mix |
| `delay.csd` | stereo ping-pong delay on inputs 1-2 | time, feedback, mix, tone |
| `quadosc.csd` | sine, triangle, saw and pulse on outputs 1-4; input 1 is pitch CV, 1 V/oct | pitch, width |
| `fm-midi.csd` | polyphonic FM synth, played over MIDI; silent until notes arrive from the input chosen in the general panel | ratio, index, attack, release |
