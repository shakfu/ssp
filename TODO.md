# TODO

## Device checks

- MIDI on `csnd` and `chuk`: see `plugins/csound/TODO.md` and `plugins/chuck/TODO.md`.

- `edrm`, `strc`, `bard` and `gltc` run on the SSP (user, 2026-10-09).

- `chrs`: listed, audible, DSP ~1.3% on the SSP (2026-10-08). Untested: CV into each control, compact editor in rack, preset save and reload.

- `fstr` 0.2.0: the `tan()` fix works on the SSP; `filter.dsp` sounds, DSP ~0.9% average (2026-10-09). `chorus.dsp` ~1.3% average, 3.3% peak, as `chrs` (3.9% before the fix). Defaults apply on Load (checked 2026-10-09). Presets work; a broken `.dsp` shows its error. Untested: compact editor in rack.
