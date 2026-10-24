# fstr

`fstr` runs Faust programs (`.dsp`) loaded from the SD card. libfaust's LLVM backend compiles each to machine code on the SSP, so no rebuild is needed per program.

## Install

Copy `fstr.so` to `BOOT/plugins` (from source: `make install MOD=fstr`, to a mounted card) and restart the SSP. See [Using](../../docs/USING.md#installing).

`BOOT/faust` holds the examples and, in `libraries`, the Faust standard libraries that `import("stdfaust.lib")` needs. A release has them there. From source, `make deps` builds libfaust once; then `make install-faust` copies both to a mounted card, or over ssh to `SSP_HOST`.

A USB keyboard edits the code; it is optional ([Keyboard](#keyboard)).

## First use

1. Add `fstr` and patch Out 1 and Out 2 to outputs. Synthor runs a module only while a jack is patched ([Using](../../docs/USING.md#a-module-runs-only-while-a-jack-is-patched)). The built-in drone plays.
2. Press Load (button 5), select `filter.dsp`, and press Load again.
3. Patch stereo audio to In 1 and In 2.
4. Turn encoder 1 to sweep the cutoff from its default, 1000 Hz, then encoder 2 for resonance.

[Walkthrough: `filter.dsp`](#walkthrough-filterdsp) explains each step.

## The language

### What it is

Faust is a functional language for block diagrams of signal processors. A program defines `process`, a function from input signals to output signals. The compiler turns it into a function that computes one sample at a time. UI elements such as `hslider` become controls. A program is a text file, `name.dsp`. See the [Faust manual](https://faustdoc.grame.fr/manual/syntax/) and the [Faust 101 tutorial](https://faustdoc.grame.fr/workshops/2020-04-10-faust-101/).

### A first program

`filter.dsp` with one control: a resonant low-pass on In 1 and In 2, to Out 1 and Out 2.

```faust
import("stdfaust.lib");

cutoff = hslider("cutoff [scale:log][unit:Hz]", 1000, 40, 16000, 1) : si.smoo;

process = par(i, 2, fi.resonlp(cutoff, 2, 1));
```

- `import("stdfaust.lib")` loads the standard libraries: `si.` smoothing, `fi.` filters.
- `hslider(label, default, min, max, step)` becomes encoder 1, from 40 to 16000 Hz.
- `: si.smoo` sends the slider through a smoother, so a fast turn does not click.
- `par(i, 2, ...)` places two filters side by side: two inputs, two outputs.

### How it maps to the SSP

| SSP | In Faust |
|-|-|
| Audio in | `process` inputs, in order, are In 1..8; see [Audio in](#audio-in) |
| Audio out | `process` outputs, in order, are Out 1..8; see [Audio out](#audio-out) |
| Encoders | first 16 `hslider`, `vslider`, `nentry`, `button`, `checkbox` become p1..p16; see [Programs](#programs) |
| CV in | `[cv:N]` in a label, once per block; or a `process` input; see [CV in](#cv-in) |
| CV out | a `process` output, at 0.2 per volt; see [CV out](#cv-out) |
| Gates and triggers | an input compared to a level, such as `x > 0.2`; `button`, `checkbox` become 0-to-1 controls |
| MIDI in | `declare options "[nvoices:N]";`: notes set `freq`, `gain`, `gate`; see [MIDI](#midi) |
| MIDI out | n/a: `fstr` sends no MIDI |
| Clock and tempo | n/a: no host tempo; count clock pulses on an input |
| Sample rate and block size | `ma.SR` is the SSP's rate; `process` runs per 128-frame block; a rate change recompiles |
| Files on the card | `.dsp` in `BOOT/faust`; `import` searches the program's folder, then `BOOT/faust/libraries` |
| Not available on the SSP | `soundfile` (no file loads); bargraphs (not shown); `[midi:...]` metadata (ignored); controls past 16 |

## Programs

Load (button 5) picks a `.dsp` file. A program that fails to compile shows its error, and the previous program keeps running. With no program, a built-in drone plays.

- **Controls**: Faust orders controls by label, or by `[0]`, `[1]`... prefixes. Each takes the control's label, range and `[unit:...]`; `[scale:log]` maps it logarithmically. A control whose minimum equals its maximum is skipped.

- **Defaults**: a program chosen with Load sets its controls to the program's defaults. A preset restores its own values.

## Keyboard

The [shared editor](../../docs/script-editor.md) edits the program, on key port 7772. Ctrl-S on the built-in writes `BOOT/faust/live.dsp`. Ctrl-E does nothing: a Faust program compiles only whole, with Ctrl-Enter. Ctrl-., Ctrl-R and Ctrl-1..0 do nothing either.

A preset stores the program as [Script modules](../../docs/script-editor.md#presets) describes.

## Inputs and outputs

In 1..In 8 and Out 1..Out 8 carry audio and CV alike, at 0.2 per volt; see [CV](../../docs/script-editor.md#cv).

### Audio in

Signals arrive unscaled, 1.0 for 5 V. A program with fewer inputs ignores the rest.

### CV in

On a control, add `[cv:N]` to its label; see [CV on a control](../../docs/script-editor.md#cv-on-a-control). The CV is read once per audio block: 128 frames, 2.7 ms at 48 kHz. That suits envelopes, LFOs and sequencers, not audio-rate modulation.

```text
cutoff = hslider("cutoff [scale:log][unit:Hz][cv:3]", 1000, 40, 16000, 1);
```

In the program, give `process` an input for the CV and use it at audio rate. `examples/filter_cv.dsp` reads In 3 as 1 V/oct:

```faust
fc(cv) = max(20, min(16000, cutoff * pow(2, cv / 0.2)));
process(l, r, cv) = (l : fi.resonlp(fc(cv), q, 1)), (r : fi.resonlp(fc(cv), q, 1));
```

[Walkthrough step 6](#6-add-cv) explains it.

### Audio out

Outputs past Out 8 are discarded. Jacks past the program's outputs are silent.

### CV out

A constant is a signal:

```faust
import("stdfaust.lib");
process = _, _, os.osc(2) * 0.5, 0.2 * octaves;   // In 1-2 to Out 1-2; Out 3: +-2.5 V at 2 Hz; Out 4: 1 V/oct
```

## MIDI

Notes come from the MIDI input chosen in the general panel (LS + RS, see [Using](../../docs/USING.md#the-general-panel)), and take effect at the next block. A program is polyphonic when its options declare a voice count:

```faust
declare options "[nvoices:8]";
freq = hslider("freq", 440, 20, 20000, 1);   // Hz, from the note
gain = hslider("gain", 0.5, 0, 1, 0.01);     // velocity / 127
gate = button("gate");                       // 1 while the note is held
```

- Each note takes a free voice, or the oldest. This is Faust's own polyphony (`mydsp_poly`).

- The voices' outputs add, unscaled: scale a voice so N of them do not clip.

- `freq`, `gain` and `gate` stay off the encoders, as do `key`, `vel` and `velocity`. Every other control drives all the voices, with its CV.

- A program without `nvoices` ignores notes.

- Once a note has arrived, the status panel counts them: `MIDI notes: 12`.

## Examples

| File | What |
|-|-|
| `chorus.dsp` | `chrs`'s chorus: compare the DSP load |
| `filter.dsp` | resonant low-pass on inputs 1 and 2 |
| `filter_cv.dsp` | `filter.dsp` with cutoff CV on input 3, 1 V/oct; see the walkthrough |
| `lfo.dsp` | four LFOs at one rate as CV on outputs 1-4: sine, triangle, saw, square, +-depth V; input 1 is rate CV (an octave per volt), input 2 depth CV |
| `osc.dsp` | band-limited saw; input 1 is pitch CV (1 V/oct), input 2 level CV, both declared with `[cv:N]` |
| `poly.dsp` | 8-voice saw synth to outputs 1-2, played over MIDI; silent until notes arrive; input 1 is cutoff CV |

## Walkthrough: `filter.dsp`

`examples/filter.dsp` is a stereo resonant low-pass. This follows it from the card to the screen.

```faust
declare name "filter";
declare description "Resonant low-pass on inputs 1 and 2.";

import("stdfaust.lib");

cutoff = hslider("cutoff [scale:log][unit:Hz]", 1000, 40, 16000, 1) : si.smoo;
q = hslider("resonance", 1, 0.5, 20, 0.01) : si.smoo;

process = par(i, 2, fi.resonlp(cutoff, q, 1));
```

### 1. Copy it to the card

Put `filter.dsp` in `BOOT/faust`, with the Faust libraries in `BOOT/faust/libraries`. The release already has both there.

### 2. Patch it in Synthor

`fstr` always has eight inputs, `In 1` to `In 8`, and eight outputs, `Out 1` to `Out 8`. The program's inputs and outputs take them in order. `par(i, 2, ...)` is two filters side by side, so `process` has two inputs and two outputs:

| In the program | `fstr` jack | Patch |
|-|-|-|
| `process` input 1, 2 | In 1, In 2 | stereo audio to filter |
| `process` output 1, 2 | Out 1, Out 2 | to the next module or an output |

### 3. Load it

Press Load (button 5). The browser opens at `BOOT/faust`. Turn encoder 1, or press Up and Down, to select `filter.dsp`. Left and Right (`PG-`, `PG+`) jump a column, and pressing encoder 1 opens a folder. Press Load again. Cancel (button 7) leaves without loading. The SSP compiles the program in a moment, and the previous program plays until it is ready.

### 4. Read the screen

```text
fstr : Faust host, through the libfaust LLVM JIT      1/1 controls     DSP n.n% pk n.n%
   cutoff       resonance                             filter.dsp
  1000 Hz         1.000
```

Each `hslider` became one control. There are no comments to write: the label, range, default and unit all come from the slider.

| In the program | Encoder | Screen | Range | Default |
|-|-|-|-|-|
| `hslider("cutoff [scale:log][unit:Hz]", 1000, 40, 16000, 1)` | 1 | `cutoff`, in Hz | 40 to 16000, logarithmic | 1000 |
| `hslider("resonance", 1, 0.5, 20, 0.01)` | 2 | `resonance` | 0.5 to 20 | 1 |

- **Order**: Faust sorts controls by label, so `cutoff` comes before `resonance`. To choose the order, prefix labels with `[0]`, `[1]`...: `hslider("[0] cutoff ...")`.

- **Metadata**: `[unit:Hz]` puts the unit on screen; `[scale:log]` makes the encoder logarithmic. Both are stripped from the label.

The right panel shows the page (`1/1 controls`), the DSP load, the file name, and a compile error in orange if there is one. If the libraries are missing, for example, it shows `ERROR : unable to open file stdfaust.lib (built-in running)`.

### 5. Play it

Turn encoder 1 to sweep the cutoff and encoder 2 for resonance. `si.smoo` smooths each change, so even a fast turn does not click.

[Encoders](../../docs/script-editor.md#encoders) gives the step sizes. `cutoff` spans about 8.6 octaves, so a detent is about 0.4 octave. Pressing returns a control to the bottom of its range, not to the slider's default.

### 6. Add CV

`[cv:3]` on the cutoff slider would sweep it from In 3, once per block. To sweep it at audio rate instead, give `process` a third input and use it as a 1 V/oct offset. `examples/filter_cv.dsp` is this program:

```faust
import("stdfaust.lib");

cutoff = hslider("cutoff [scale:log][unit:Hz]", 1000, 40, 16000, 1) : si.smoo;
q = hslider("resonance", 1, 0.5, 20, 0.01) : si.smoo;

// In 3 is cutoff CV, 1 V/oct; the SSP reads 0.2 per volt
fc(cv) = max(20, min(16000, cutoff * pow(2, cv / 0.2)));

process(l, r, cv) = (l : fi.resonlp(fc(cv), q, 1)), (r : fi.resonlp(fc(cv), q, 1));
```

`process(l, r, cv)` names the three inputs, so In 3 is `cv`. The parentheses matter: in Faust `,` binds tighter than `:`.

### 7. Save it

Save a Synthor preset as usual. It stores the file's path and the encoder positions; loading the preset compiles `filter.dsp` again and restores the positions.
