# faust

`faust` runs Faust programs (`.dsp`) loaded from the SD card. libfaust's LLVM backend compiles each to machine code on the SSP, so no rebuild is needed per program. It links the SSP's own `libLLVM-9.so`. See [docs/dev/faust.md](../../docs/dev/faust.md).

## Install

1. `make deps` builds libfaust, once.
2. `make install MOD=fstr` copies `fstr.so` to the card's `plugins` folder.
3. `make install-faust` copies the Faust libraries to `BOOT/faust/libraries` and the examples to `BOOT/faust`.

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
| `osc.dsp` | band-limited saw, V/oct on input 1 |
