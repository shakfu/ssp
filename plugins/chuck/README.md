# chuck

`chuck` runs a [ChucK](https://chuck.stanford.edu) program (`.ck`) on the SSP. The program gets eight audio inputs and outputs, sixteen controls, and ChucK's own MIDI.

Ported from the `chuck` engine in sk-engines; see [docs/dev/engines.md](../../docs/dev/engines.md).

## Install

Copy `chuk.so` to the `plugins` folder on the SD card. The module is `chuk`: the SSP lists a module only if its id spells its name (see [docs/dev/engines.md](../../docs/dev/engines.md)). ChucK is linked in; nothing else is needed. Put programs anywhere on the card; the browser opens at `/media/BOOT/chuck`.

## Use

Load (button 5) opens a browser. Choose a `.ck` and press Load again. The screen shows the file, and the compile error if there is one. A program that fails to compile leaves the previous one running. With no program, a built-in drone plays: pitch, level and cutoff.

Audio stops while a program compiles. The preset stores the file's path, not its contents.

## The program

| | |
|-|-|
| controls | `global float p1;` .. `p16`, updated every 10 ms; see Labels and ranges |
| audio in | `adc.chan(0)` .. `adc.chan(7)` |
| audio out | `dac.chan(0)` .. `dac.chan(7)`; `=> dac` feeds all eight |
| MIDI | `MidiIn` and `MidiOut` on the SSP's ALSA devices |
| files | `me.dir()` is the program's folder |

- A new program replaces every shred, and every connection into `dac` and `blackhole`.
- Globals keep their values across programs.
- A `public class` stays defined until the plugin is reloaded, so reloading a program that defines one fails.
- `<<< >>>` and `chout` print nowhere.

## Labels and ranges

A comment line names a control and gives its range; the screen then shows the name, the value in that range and the unit, and the program receives the value in that range:

```
// @p1 cutoff 20 20000 Hz log
// @p2 mix
```

The form is `@pN label [min max [unit]] [log]`, on a line that starts with `//`. The label is one word. Without a range a control is 0 to 1. `log` maps the encoder logarithmically and needs a positive range. The encoder pages show only the declared controls, four to a page, in order; a program that declares none shows all sixteen as `P1`..`P16`. Every control still exists for presets, MIDI learn and rack, and the parameter ids stay `p1`..`p16`; a preset stores the encoder position, so it restores the same position in a program with a different range.

## Examples

`examples/` holds programs to copy to the card's `chuck/` folder. `make test` compiles and runs each one.

| File | What | Controls |
|-|-|-|
| `filter.ck` | stereo resonant lowpass on inputs 1-2; input 3 is cutoff CV, 1 V/oct | cutoff, Q |
| `sequencer.ck` | 8-step random melody; output 3 is a gate, output 4 pitch CV, 1 V/oct | tempo, octaves, decay, reroll |
| `midi.ck` | 8-voice saw synth on MIDI input device 0 | cutoff, release |
