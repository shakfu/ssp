# Writing a Faust plugin

A [Faust](https://faust.grame.fr) program reaches the SSP in two ways:

- **A. Compiled into a module of its own**, like `chrs`. [cyfaust](https://github.com/shakfu/cyfaust) compiles the `.dsp` to C++ on your computer, and the build links it into a plugin.
- **B. Loaded by `fstr`** from the SD card. libfaust compiles it on the SSP, through LLVM, when you press Load.

Both tutorials use the same program, a stereo tremolo. [DEVELOPING.md](DEVELOPING.md#choosing-a-route) compares the routes; [dev/faust.md](dev/faust.md) records how they were built and measured.

```faust
declare name "tremolo";
declare description "Stereo tremolo: a sine LFO on the level of inputs 1 and 2.";

import("stdfaust.lib");

rate = hslider("[0] rate [unit:Hz]", 4, 0.1, 20, 0.01);
depth = hslider("[1] depth", 0.5, 0, 1, 0.01) : si.smoo;

gain = 1 - depth * (0.5 + 0.5 * os.osc(rate));

process = par(i, 2, *(gain));
```

- `process` has two inputs and two outputs: `par(i, 2, ...)` places two copies side by side.
- Each `hslider` becomes one encoder. `vslider`, `nentry`, `button` and `checkbox` do too.
- Faust sorts controls by label. The `[0]` and `[1]` prefixes keep `rate` first; Faust removes them from the label.
- `si.smoo` smooths `depth`, so a fast turn does not click.

## The two routes

| | A: compiled module | B: `fstr` program |
|-|-|-|
| change the program | edit, rebuild, reinstall, restart | edit on the card, press Load |
| jacks | the program's inputs, a CV input per control, the program's outputs | always In 1..8 and Out 1..8 |
| CV | 1.0 (5 V) spans the control's range; read once per block | none per control: read CV as an audio input |
| controls | all, four to a page | the first 16 |
| `[unit:...]`, `[scale:log]` | ignored: no unit on screen, linear | unit on screen, logarithmic |
| parameter ids in presets | the labels, lower case: `rate`, `depth` | `p1`..`p16` |
| encoder steps | 5% of the range, 0.5% while held | the same |
| on screen in Synthor | its own name: `trem` | `fstr`, and the file name |

## Tutorial A: a compiled module

This builds `trem`. The finished plugin is in [`examples/tremolo`](../examples/tremolo); `make test` builds it for the host and checks its output.

To skip steps A1 to A5:

```
scripts/new_plugin.py wobble wobl --faust
```

This copies `examples/tremolo` to `plugins/wobble` as the module `wobl`: `Source/wobble.dsp`, its kernel, the processor, CMake, README and CHANGELOG stubs. It adds the folder to `plugins/CMakeLists.txt` and the kernel to `make faust-kernels`. Edit `wobble.dsp`, run `make faust-kernels`, and build.

### A1. Write the program

Pick a name of at most four characters, and use its capitals as the uid: `trem`/`TREM`. Synthor lists a plugin only if its uid spells its name.

Save the program as `plugins/tremolo/Source/tremolo.dsp`.

### A2. Compile the kernel

```
scripts/faust_kernel.sh plugins/tremolo/Source/tremolo.dsp plugins/tremolo/Source/TremoloKernel.h tremolo
```

The arguments are the program, the header to write, and a C++ namespace. The script runs cyfaust through `uv`, which fetches it on first use. The header holds `class mydsp` in namespace `ssp::faust::tremolo`, so no two plugins define the same `mydsp`.

Commit the header: the SSP build does not run Faust. Add the same line to the `faust-kernels` target in the `Makefile`, so `make faust-kernels` regenerates it after an edit.

### A3. The processor

`Source/PluginProcessor.h` is the whole processor. `FaustProcessor` builds the jacks, parameters and pages from the kernel.

```cpp
#pragma once

#include "TremoloKernel.h"
#include "engine/FaustProcessor.h"

using namespace juce;

class PluginProcessor : public ssp::faust::FaustProcessor {
public:
    using Engine = ssp::faust::FaustEngine<ssp::faust::tremolo::mydsp>;
    static juce::Colour colour() { return { 120, 220, 120 }; }

    PluginProcessor() : FaustProcessor(new Engine, colour()) {}

    // SSPApi.h names the channels before an instance exists
    static BusesProperties getBusesProperties() { return buses(Engine().spec()); }
};
```

`SSPApi.h` requires the class name `PluginProcessor`.

### A4. The SSP entry points

Copy [`examples/tremolo/Source/SSPApi.cpp`](../examples/tremolo/Source/SSPApi.cpp). It reads the colour from `PluginProcessor::colour()`. Change the category if it is not an effect: the `CAT_` macros are in `plugins/common/SSPApi.h`.

### A5. CMake

Copy [`examples/tremolo/CMakeLists.txt`](../examples/tremolo/CMakeLists.txt) and change the names:

| Field | Value |
|-|-|
| `project(...)` | `TREMOLO`, the CMake target; also in `juce_add_plugin` and the three `target_` calls |
| `DESCRIPTION` | `stereo tremolo, compiled from Faust`: shown on the editor's title line |
| `PLUGIN_CODE` | `TREM` |
| `PRODUCT_NAME` | `trem` |

Its sources are `SSPApi.cpp` and three source lists from `plugins/common`: `${COMMON_SRC}`, `${ENGINE_SRC}` and `${FAUST_SRC}`. Then add the folder to `plugins/CMakeLists.txt`:

```cmake
add_subdirectory(tremolo)
```

### A6. Build and install

```
make                    # cross build for the SSP
make install MOD=trem   # copy trem.so to the mounted card's BOOT/plugins
```

Restart the SSP and add `trem` in Synthor. Its jacks:

| Jack | From the program |
|-|-|
| In L, In R | `process` inputs 1 and 2 |
| rate, depth | one CV per control, named by its label |
| Out L, Out R | `process` outputs 1 and 2 |

```
trem : stereo tremolo, compiled from Faust          1/1 trem        DSP n.n% pk n.n%
     rate          depth
     4.00          0.50
```

Encoder 1 is `rate`, encoder 2 is `depth`. Each starts at its slider's default. A CV adds to its encoder's value: 1.0 (5 V) on `depth` moves it across its whole range.

### A7. Test

Add the plugin to `PRODUCTS` in `plugins/common/tests/test_engine_plugins.py`, then a test. Inputs are numbered from 0, so the `depth` CV is input 3:

```python
def test_tremolo(plugins):
    # depth 0 passes the input; depth 1 starts the LFO near half level
    levels, _ = run(plugins, "tremolo", "set", "depth", 0, "prepare", 48000, 128, "in", 0, 0.3, "run", 0.2, 128,
                    "level", 0)
    assert levels[0][1] == pytest.approx(0.3, abs=1e-4)
    # inputs: In L, In R, then a CV per control (rate, depth); a depth CV of 1.0 spans the range
    levels, _ = run(plugins, "tremolo", "set", "depth", 0, "set", "rate", 0.1, "prepare", 48000, 128, "in", 0, 0.3,
                    "in", 3, 1.0, "run", 0.2, 128, "level", 0)
    assert 0.1 < levels[0][1] < 0.16
```

Rewrite rack's module manifest, which the manifest test checks:

```
UPDATE_MANIFEST=1 uv run --with pytest pytest plugins/common/tests -k manifest
```

[CPP_PLUGINS.md](CPP_PLUGINS.md#7-test) lists `plugin_host`'s commands. `plugins/chorus/tests` also builds the kernel natively, with a plain compiler, and checks its DSP.

## Tutorial B: a program for `fstr`

The same `tremolo.dsp`, without a build.

### B1. Check it compiles

On your computer, before copying it to the card:

```
PYTHONUTF8=1 uv run --no-project --with cyfaust==0.2.0 python -m cyfaust info tremolo.dsp
```

It prints the inputs, outputs and controls, or Faust's error. On the SSP the same error appears in the status panel, and the previous program keeps playing.

### B2. Copy it to the card

```
make install-faust                                  # once: the Faust libraries, in BOOT/faust/libraries
cp tremolo.dsp /media/$USER/BOOT/faust/
```

`import("stdfaust.lib")` looks in the program's folder, then in `BOOT/faust/libraries`.

### B3. Load it

Add `fstr` in Synthor. Press soft key 5 (Load), select `tremolo.dsp` with encoder 1, and press Load again. The program's inputs and outputs take `fstr`'s jacks in order:

| In the program | `fstr` jack |
|-|-|
| `process` inputs 1, 2 | In 1, In 2 |
| `process` outputs 1, 2 | Out 1, Out 2 |

```
fstr : Faust host, through the libfaust LLVM JIT    1/1 controls    DSP n.n% pk n.n%
     rate          depth                            tremolo.dsp
   4.00 Hz         0.500
```

Load sets each control to its slider's default. Unlike `trem`, `fstr` shows `[unit:Hz]`, and would map a `[scale:log]` slider logarithmically. A preset stores the file's path and the encoder positions, as `p1` and `p2`.

### B4. Add CV

`fstr` has no CV input per control. To modulate `depth` from In 3, make it a third input:

```faust
process(l, r, cv) = l * g, r * g
with {
    g = 1 - max(0, min(1, depth + cv)) * (0.5 + 0.5 * os.osc(rate));
};
```

`process(l, r, cv)` names the three inputs, so In 3 is `cv`, and 1.0 (5 V) adds 1 to `depth`. [plugins/faust/README.md](../plugins/faust/README.md#walkthrough-filterdsp) walks through `filter.dsp` the same way.

### B5. Make it a module

When the program is done, Tutorial A turns the same file into a module: CV per control, its own name, and no compile on Load.
