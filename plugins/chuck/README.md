# chuck

`chuck` runs a [ChucK](https://chuck.stanford.edu) program (`.ck`) on the SSP. The program gets eight audio inputs and outputs, sixteen controls, and ChucK's own MIDI.

Ported from the [`chuck` engine](https://github.com/shakfu/sk-engines/tree/main/src/engine/chuck) in [sk-engines](https://github.com/shakfu/sk-engines); see [docs/dev/engines.md](../../docs/dev/engines.md).

## Install

Copy `chuk.so` to the `plugins` folder on the SD card. The module is `chuk`: the SSP lists a module only if its id spells its name (see [docs/dev/engines.md](../../docs/dev/engines.md)). ChucK is linked in; nothing else is needed. Put programs anywhere on the card; the browser opens at `/media/BOOT/chuck`.

## Use

Load (button 5) opens a browser. Choose a `.ck` and press Load again. The screen shows the file, and the compile error if there is one. A program that fails to compile leaves the previous one running. With no program, a built-in drone plays: pitch, level and cutoff.

Audio stops while a program compiles. The preset stores the file's path, not its contents.

## The program

| | |
|-|-|
| controls | `global float p1;` .. `p16`, updated every 10 ms; see Labels and ranges |
| audio and CV | see Inputs and outputs |
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

The form is `@pN label [min max [unit]] [log] [cv N]`, on a line that starts with `//`. The label is one word. Without a range a control is 0 to 1. `log` maps the encoder logarithmically and needs a positive range. The encoder pages show only the declared controls, four to a page, in order; a program that declares none shows all sixteen as `P1`..`P16`. Every control still exists for presets, MIDI learn and rack, and the parameter ids stay `p1`..`p16`; a preset stores the encoder position, so it restores the same position in a program with a different range.

`cv N` makes input N move the control; see [CV in](#cv-in).

## Inputs and outputs

Inputs 1..8 and outputs 1..8 carry audio and CV alike. An SSP signal of 1.0 is 5 V, so CV is 0.2 per volt: 1 V is 0.2, and 1 V/oct pitch is 0.2 per octave.

### Audio in

`adc.chan(0)` .. `adc.chan(7)`: input 1 is `adc.chan(0)`. Signals arrive unscaled, 1.0 for 5 V.

### CV in

A program gets CV in two ways: on a control, or by reading the input itself.

#### On a control

A control with CV follows input N (1..8): add `cv N` at the end of its `@pN` line.

```
// @p1 cutoff 20 20000 Hz log cv 3
```

| Range | Each volt | -1 V | +1 V | +5 V |
|-|-|-|-|-|
| log | an octave | half | double | 32x |
| linear | a tenth of the range | -10% of the range | +10% | +50% |

- The encoder sets the value the CV moves from. The result stays within the control's range.
- The CV reaches the program every 10 ms, when the globals `p1`..`p16` update. That is fine for envelopes, slow LFOs and sequencers, not for audio-rate modulation.
- An orange mark on the control's bar shows where the CV has moved it, and the value shown is the moved one. A preset stores the encoder position, not the moved value.
- The input still reaches the program as audio.

#### In the program

Read the input yourself, for a scaling of your own. There is no mark on screen. `examples/filter.ck` keeps In 3 running into `blackhole` and reads its last sample:

```
adc.chan(2) => Gain cv => blackhole;
p1 * Math.pow(2, cv.last() / 0.2) => float f;   // SSP CV is 0.2 per volt
```

[Walkthrough step 6](#6-add-cv) explains it.

### Audio out

`dac.chan(0)` .. `dac.chan(7)`; `=> dac` feeds all eight.

### CV out

Write a value or a slow signal to an output, at 0.2 per volt. `Step` holds a value:

```
SinOsc lfo => dac.chan(2);  2 => lfo.freq;  0.5 => lfo.gain;   // Out 3: +-2.5 V at 2 Hz
Step pitch => dac.chan(3);  0.2 * octaves => pitch.next;      // Out 4: 1 V/oct
```

## Examples

`examples/` holds programs to copy to the card's `chuck/` folder. `make test` compiles and runs each one.

| File | What | Controls |
|-|-|-|
| `filter.ck` | stereo resonant lowpass on inputs 1-2; input 3 is cutoff CV, 1 V/oct | cutoff, Q |
| `sequencer.ck` | 8-step random melody; output 3 is a gate, output 4 pitch CV, 1 V/oct; input 1 is tempo CV, input 2 decay CV, both declared with `cv` | tempo, octaves, decay, reroll |
| `midi.ck` | 8-voice saw synth on MIDI input device 0 | cutoff, release |

## Walkthrough: `filter.ck`

`examples/filter.ck` is a stereo resonant lowpass. This follows it from the card to the screen.

```chuck
// Stereo resonant lowpass on inputs 1 and 2; input 3 is cutoff CV, 1 V/oct.
// @p1 cutoff 40 20000 Hz log
// @p2 Q 1 16
global float p1, p2;

adc.chan(0) => LPF l => dac.chan(0);
adc.chan(1) => LPF r => dac.chan(1);
adc.chan(2) => Gain cv => blackhole;

while (true) {
    p1 * Math.pow(2, cv.last() / 0.2) => float f;   // SSP CV is 0.2 per volt
    Math.min(Math.max(f, 20), (second / samp) * 0.45) => f;
    f => l.freq => r.freq;
    Math.max(p2, 1) => l.Q => r.Q;
    1::ms => now;
}
```

### 1. Copy it to the card

Put `filter.ck` in `chuck/` on the card's BOOT partition. The release already has it there.

### 2. Patch it in Synthor

`chuk` always has eight inputs, `In 1` to `In 8`, and eight outputs, `Out 1` to `Out 8`. ChucK numbers channels from 0:

| In the program | `chuk` jack | Patch |
|-|-|-|
| `adc.chan(0)`, `adc.chan(1)` | In 1, In 2 | stereo audio to filter |
| `adc.chan(2)` | In 3 | cutoff CV, 1 V/oct |
| `dac.chan(0)`, `dac.chan(1)` | Out 1, Out 2 | to the next module or an output |

### 3. Load it

Press soft key 5 (Load). The browser opens at `/media/BOOT/chuck`. Turn encoder 1, or press Up and Down, to select `filter.ck`; Left and Right jump a column, and pressing encoder 1 opens a folder. Press Load again. Soft key 7 (Cancel) leaves without loading. Audio stops while the program compiles.

### 4. Read the screen

```
chuk : ChucK host                                     1/1 controls     DSP n.n% pk n.n%
   cutoff          Q                                  filter.ck
  40.0 Hz        1.00
```

Each `@p` comment became one control, in order:

| Declaration | Encoder | Screen | Range | The program reads |
|-|-|-|-|-|
| `// @p1 cutoff 40 20000 Hz log` | 1 | `cutoff`, in Hz | 40 to 20000, logarithmic | `global float p1` |
| `// @p2 Q 1 16` | 2 | `Q` | 1 to 16 | `global float p2` |

The comments only set what the screen shows and the range the program receives. The program must still declare `global float p1, p2;` to read them. `chuk` writes the globals every 10 ms; this program reads them every millisecond.

The right panel shows the page (`1/1 controls`), the DSP load, the file name, and a compile error in orange if there is one.

### 5. Play it

Both controls start at the bottom of their ranges: the cutoff at 40 Hz, which passes little, and Q at 1. Turn encoder 1 to open the filter, then encoder 2 for resonance.

- Turning moves a control 5% of its range a detent. `cutoff` is logarithmic, so each detent is the same interval: 40 to 20000 Hz is about 9 octaves, so a detent is about half an octave.
- Holding the encoder while turning moves it 0.5% a detent.
- Pressing and releasing without turning returns it to the bottom of its range.

### 6. Add CV

`adc.chan(2) => Gain cv => blackhole` keeps In 3 running without sending it anywhere audible, so `cv.last()` can read it. The program multiplies the cutoff by `2 ^ (In 3 / 0.2)`: each volt raises the cutoff an octave. It then limits it to between 20 Hz and 45% of the sample rate.

### 7. Save it

Save a Synthor preset as usual. It stores the file's path and the encoder positions; loading the preset compiles `filter.ck` again and restores the positions.

