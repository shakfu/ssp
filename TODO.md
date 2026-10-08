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
