# TODO

## Device checks

- MIDI on `csnd` and `chuk`: see `plugins/csound/TODO.md` and `plugins/chuck/TODO.md`.
- `edrm`, `strc`, `bard` and `gltc` are ported, tested on the host and cross-built, but not yet run on the SSP. Per module:
  - It is listed in Synthor, loads, and its full and compact editors draw.
  - DSP load on screen with both decks or all tracks busy. For `strc`, at each window size: 32768 may not fit.
  - `edrm`: the SSP clock into Clock steps the tracks; the clock's level crosses the 2 V threshold.
  - `strc`, `bard`: files load from the card; Load picks the folder.
  - `bard`: `resume.txt` is written to the card and a book resumes after a power cycle.
  - `bard`: buttons 6 and 8 (Next) work beside Load and Cancel.
- `chrs`: listed, audible, DSP ~1.3% on the SSP (2026-10-08). Untested: CV into each control, compact editor in rack, preset save and reload.
- `fstr` 0.2.0: the `tan()` fix works on the SSP; `filter.dsp` sounds, DSP ~0.9% average (2026-10-09). `chorus.dsp` ~1.3% average, 3.3% peak, as `chrs` (3.9% before the fix). Also: a Load left `filter.dsp`'s controls at 0, not its defaults. Untested: broken `.dsp` error; presets; compact editor.
