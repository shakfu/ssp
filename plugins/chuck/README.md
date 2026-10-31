# chuk

`chuk` runs a [ChucK](https://chuck.stanford.edu) program (`.ck`). The program gets eight audio inputs and outputs, sixteen controls, and MIDI notes.

Ported from the [`chuck` engine](https://github.com/shakfu/sk-engines/tree/main/src/engine/chuck) in [sk-engines](https://github.com/shakfu/sk-engines).

## Install

Copy `chuk.so` to `BOOT/plugins` (from source: `make install MOD=chuk`, to a mounted card) and restart the SSP. See [Using](../../docs/USING.md#installing).

The example programs go in `BOOT/chuck`, where the browser opens. A release has them there. From source, copy `examples/` to `BOOT/chuck` by hand. Programs can sit anywhere on the card.

A USB keyboard edits the code; it is optional ([Keyboard](#keyboard)).

## First use

1. Add `chuk` and patch Out 1 and Out 2 to outputs. Synthor runs a module only while a jack is patched ([Using](../../docs/USING.md#a-module-runs-only-while-a-jack-is-patched)). The built-in drone plays.
2. Press Load (button 5), select `filter.ck`, and press Load again.
3. Patch stereo audio to In 1 and In 2.
4. Turn encoder 1 (`cutoff`) up from 40 Hz to open the filter, then encoder 2 for resonance.

[Walkthrough: `filter.ck`](#walkthrough-filterck) explains each step.

## The language

### What it is

ChucK is strongly timed: a program advances time itself, by chucking a duration to `now`. A program is a `.ck` file of statements that run top to bottom as one shred, ChucK's name for a thread. `=>` (chuck) connects unit generators, such as `adc => LPF => dac`, and assigns values. See the [language reference](https://chuck.stanford.edu/doc/reference/) and, for beginners, [Learning ChucK](https://chuck.stanford.edu/doc/learn/).

### A first program

A mono lowpass filter from In 1 to Out 1, with its cutoff on encoder 1:

```chuck
// @p1 cutoff 40 20000 Hz log
global float p1;                       // encoder 1, in Hz

adc.chan(0) => LPF f => dac.chan(0);   // In 1 to Out 1

while (true) {
    Math.max(p1, 20) => f.freq;        // p1 is 0 until the module sets it
    10::ms => now;
}
```

- `// @p1 cutoff 40 20000 Hz log` names encoder 1 and sets its range.
- `global float p1;` receives that control, in Hz.
- `adc.chan(0)` is In 1; `dac.chan(0)` is Out 1.
- `10::ms => now` advances time 10 ms, then the loop reads `p1` again.

### How it maps to the SSP

| SSP | In ChucK |
|-|-|
| Audio in | `adc.chan(0)` .. `adc.chan(7)` are In 1..In 8. [Audio in](#audio-in) |
| Audio out | `dac.chan(0)` .. `dac.chan(7)` are Out 1..Out 8; `=> dac` feeds all. [Audio out](#audio-out) |
| Encoders | `global float p1;` .. `p16`, written every 10 ms. [Labels and ranges](#labels-and-ranges) |
| CV in | `cv N` on an `@pN` line, or `adc.chan(N-1)` / 0.2 for volts. [CV in](#cv-in) |
| CV out | `Step` or a UGen into `dac.chan(N)`, 0.2 per volt. [CV out](#cv-out) |
| Gates and triggers | in: compare `.last()` of an `adc.chan`; out: `1 => step.next` (5 V) |
| MIDI in | `global int midiNotes[128]`, `midiCount`, `Event midiEvent`; notes only. [MIDI](#midi) |
| MIDI out | `MidiOut` opens an ALSA device itself. [MIDI](#midi) |
| Clock and tempo | n/a: no clock from Synthor; patch one to an input |
| Sample rate and block size | the host's rate (48 kHz), as `second / samp`; time is sample-accurate within a block |
| Files on the card | `me.dir()` is the program's folder; `@import` resolves there |
| Not available on the SSP | `KBHit`, `Hid`, chugins; `<<< >>>` and `chout` print nowhere. [The program](#the-program) |

## Use

Load (button 5) opens a browser. Choose a `.ck` and press Load again. The screen shows the file, and the compile error if there is one. A program that fails to compile leaves the previous one running. With no program, a built-in drone plays, with controls pitch, level and cutoff.

Audio stops while a program compiles.

A preset stores the program as [Script modules](../../docs/script-editor.md#presets) describes; it does not store Ctrl-E shreds.

## Keyboard

The [shared editor](../../docs/script-editor.md) edits the program, on key port 7774. Ctrl-S on the built-in writes `BOOT/chuck/live.ck`. Ctrl-R does nothing. These keys differ:

| Key | Does |
|-|-|
| Ctrl-E | runs the block around the cursor as a new shred beside the program |
| Ctrl-1 .. Ctrl-9, Ctrl-0 | removes Ctrl-E shred 1 to 10, numbered in the order added |
| Ctrl-- | removes the newest Ctrl-E shred |
| Ctrl-. | removes every shred, the program's too: silence until the next Ctrl-Enter |

The code is coloured by syntax: types, keywords, numbers, strings, comments.

The Code view lists the Ctrl-E shreds still running above the status line; a shred that ends leaves the list. Ctrl-Enter replaces the program and ends every shred. Up to 16 run at once.

## The program

- A new program replaces every shred, and every connection into `dac` and `blackhole`.

- Globals keep their values across programs.

- A `public class` stays defined until the plugin is reloaded, so reloading a program that defines one fails.

- `KBHit` and `Hid` are compiled out: their threads would outlive the module.

- Chugins do not load.

## MIDI

Notes come from the MIDI input chosen in the general panel (LS + RS, see [Using](../../docs/USING.md#the-general-panel)). A program reads them from three globals:

```chuck
global int midiNotes[128];   // note << 8 | velocity; velocity 0 is a note off
global int midiCount;        // notes written so far; note k is at midiNotes[k % 128]
global Event midiEvent;      // broadcast after each note
```

Wait on `midiEvent`, then read every note from the last count read up to `midiCount`; several can arrive between two wakes. Start from the current `midiCount`: the globals keep earlier programs' notes. `examples/midi.ck` does this. Once a note has arrived, the status panel counts them: `MIDI notes: 12`.

`MidiIn` and `MidiOut` open an ALSA device themselves, whatever the general panel chooses.

## Labels and ranges

A comment line names a control and gives its range; the screen then shows the name, the value in that range and the unit, and the program receives the value in that range:

```text
// @p1 cutoff 20 20000 Hz log
// @p2 mix
```

The line starts with `//`. [`@pN` lines](../../docs/script-editor.md#pn-lines) gives the form and its rules.

## Inputs and outputs

In 1..In 8 and Out 1..Out 8 carry audio and CV alike, at 0.2 per volt; see [CV](../../docs/script-editor.md#cv).

### Audio in

`adc.chan(0)` .. `adc.chan(7)`: input 1 is `adc.chan(0)`. Signals arrive unscaled, 1.0 for 5 V.

### CV in

On a control, add `cv N` at the end of its `@pN` line; see [CV on a control](../../docs/script-editor.md#cv-on-a-control). The CV reaches the program every 10 ms, when the globals `p1`..`p16` update. That suits envelopes, slow LFOs and sequencers, not audio-rate modulation.

```text
// @p1 cutoff 20 20000 Hz log cv 3
```

In the program, read the input yourself. `examples/filter.ck` keeps In 3 running into `blackhole` and reads its last sample:

```text
adc.chan(2) => Gain cv => blackhole;
p1 * Math.pow(2, cv.last() / 0.2) => float f;   // SSP CV is 0.2 per volt
```

[Walkthrough step 6](#6-add-cv) explains it.

### Audio out

`=> dac` feeds all eight outputs.

### CV out

`Step` holds a value:

```text
SinOsc lfo => dac.chan(2);  2 => lfo.freq;  0.5 => lfo.gain;   // Out 3: +-2.5 V at 2 Hz
Step pitch => dac.chan(3);  0.2 * octaves => pitch.next;      // Out 4: 1 V/oct
```

## Examples

`examples/` holds programs to copy to `BOOT/chuck`. `make test` compiles and runs each one.

| File | What | Controls |
|-|-|-|
| `filter.ck` | stereo resonant lowpass on inputs 1-2; input 3 is cutoff CV, 1 V/oct | cutoff, Q |
| `sequencer.ck` | 8-step random melody; output 3 is a gate, output 4 pitch CV, 1 V/oct; input 1 is tempo CV, input 2 decay CV, both declared with `cv` | tempo, octaves, decay, reroll |
| `lfo.ck` | four LFOs at one rate as CV on outputs 1-4: sine, triangle, saw, square, +-depth V; input 1 is rate CV (an octave per volt), input 2 depth CV | rate, depth |
| `midi.ck` | 8-voice saw synth to outputs 1-2, played over MIDI; silent until notes arrive; input 1 is cutoff CV | cutoff, release |

## Walkthrough: `filter.ck`

`examples/filter.ck` is a stereo resonant lowpass. It extends [A first program](#a-first-program) with Q and cutoff CV. This follows it from the card to the screen.

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

Put `filter.ck` in `BOOT/chuck`. The release already has it there.

### 2. Patch it in Synthor

`chuk` always has eight inputs, `In 1` to `In 8`, and eight outputs, `Out 1` to `Out 8`. ChucK numbers channels from 0:

| In the program | `chuk` jack | Patch |
|-|-|-|
| `adc.chan(0)`, `adc.chan(1)` | In 1, In 2 | stereo audio to filter |
| `adc.chan(2)` | In 3 | cutoff CV, 1 V/oct |
| `dac.chan(0)`, `dac.chan(1)` | Out 1, Out 2 | to the next module or an output |

### 3. Load it

Press Load (button 5). The browser opens at `BOOT/chuck`. Turn encoder 1, or press Up and Down, to select `filter.ck`. Left and Right (`PG-`, `PG+`) jump a column, and pressing encoder 1 opens a folder. Press Load again. Cancel (button 7) leaves without loading. Audio stops while the program compiles.

### 4. Read the screen

```text
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

[Encoders](../../docs/script-editor.md#encoders) gives the step sizes. `cutoff` spans about 9 octaves, so a detent is about half an octave.

### 6. Add CV

`adc.chan(2) => Gain cv => blackhole` keeps In 3 running without sending it anywhere audible, so `cv.last()` can read it. The program multiplies the cutoff by `2 ^ (In 3 / 0.2)`: each volt raises the cutoff an octave. It then limits it to between 20 Hz and 45% of the sample rate.

### 7. Save it

Save a Synthor preset as usual. It stores the file's path and the encoder positions; loading the preset compiles `filter.ck` again and restores the positions.

## Known issues

Each module added and deleted leaves about 2.9 MB allocated until Synthor restarts, as ChucK does not free a deleted VM's types. The SSP has about 1.5 GB free, so this matters only after hundreds of cycles.
