# faust

`faust` runs Faust programs (`.dsp`) loaded from the SD card. libfaust's LLVM backend compiles each to machine code on the SSP, so no rebuild is needed per program. It links the SSP's own `libLLVM-9.so`. See [docs/dev/faust.md](../../docs/dev/faust.md).

## Install

Copy `fstr.so` to the `plugins` folder on the SD card, and the `faust` folder to the card's BOOT partition (`/media/BOOT/faust`): it holds the examples and, in `libraries`, the Faust standard libraries that `import("stdfaust.lib")` needs. `fstr` uses the SSP's own `/usr/lib/libLLVM-9.so`, part of its system.

From source: `make deps` builds libfaust once; then `make install MOD=fstr` and `make install-faust`.

## Programs

Load (button 5) picks a `.dsp` file. A program that fails to compile shows its error, and the previous program keeps running. With no program, a built-in drone plays.

- **Channels**: the program's inputs are In 1..8, its outputs Out 1..8. Channels past 8 are silent.
- **Controls**: the program's first 16 sliders, number entries, buttons and checkboxes become p1..p16, in Faust's order: by label, or by `[0]`, `[1]`... prefixes. Each takes the control's label, range and `[unit:...]`; `[scale:log]` maps it logarithmically.
- **Defaults**: a program chosen with Load sets its controls to the program's defaults. A preset restores its own values.
- **Imports**: `import("stdfaust.lib")` and the other libraries resolve in the program's folder, then in `BOOT/faust/libraries`.
- **CV**: there are no CV inputs per control. A program reads CV as an audio input; `osc.dsp` reads V/oct on input 1 (0.2 per volt).

## Examples

| File | What |
|-|-|
| `chorus.dsp` | `chrs`'s chorus: compare the DSP load |
| `filter.dsp` | resonant low-pass on inputs 1 and 2 |
| `filter_cv.dsp` | `filter.dsp` with cutoff CV on input 3, 1 V/oct; see the walkthrough |
| `osc.dsp` | band-limited saw, V/oct on input 1 |

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

Put `filter.dsp` in `faust/` on the card's BOOT partition, with the Faust libraries in `faust/libraries`. The release already has both there.

### 2. Patch it in Synthor

`fstr` always has eight inputs, `In 1` to `In 8`, and eight outputs, `Out 1` to `Out 8`. The program's inputs and outputs take them in order. `par(i, 2, ...)` is two filters side by side, so `process` has two inputs and two outputs:

| In the program | `fstr` jack | Patch |
|-|-|-|
| `process` input 1, 2 | In 1, In 2 | stereo audio to filter |
| `process` output 1, 2 | Out 1, Out 2 | to the next module or an output |

### 3. Load it

Press soft key 5 (Load). The browser opens at `/media/BOOT/faust`. Turn encoder 1, or press Up and Down, to select `filter.dsp`; Left and Right jump a column, and pressing encoder 1 opens a folder. Press Load again. Soft key 7 (Cancel) leaves without loading. The SSP compiles the program in a moment, and the previous program plays until it is ready.

### 4. Read the screen

```
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
- **Defaults**: a Load sets each control to its slider's default. A preset restores its own positions instead.

The right panel shows the page (`1/1 controls`), the DSP load, the file name, and a compile error in orange if there is one. If the libraries are missing, for example, it shows `ERROR : unable to open file stdfaust.lib (built-in running)`.

### 5. Play it

Turn encoder 1 to sweep the cutoff and encoder 2 for resonance. `si.smoo` smooths each change, so even a fast turn does not click.

- Turning moves a control 5% of its range a detent. `cutoff` is logarithmic, so each detent is the same interval: 40 to 16000 Hz is about 8.6 octaves, so a detent is about 0.4 octave.
- Holding the encoder while turning moves it 0.5% a detent.
- Pressing and releasing without turning returns a control to the bottom of its range, not to the slider's default.

### 6. Add CV

`fstr` gives a program no CV input per control: CV arrives as audio, on an input. To sweep the cutoff from In 3, give `process` a third input and use it as a 1 V/oct offset. `examples/filter_cv.dsp` is this program:

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

