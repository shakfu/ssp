# TODO

## Device checks

- MIDI on `csnd`, `chuk`, `fstr` and `scsy` works on the SSP with `midisend` (2026-10-11). Open: the inverted `BaseProcessor` device lock. See [docs/dev/midi-review.md](docs/dev/midi-review.md).

- `scsy` runs in Synthor (user, 2026-10-10): listed, audible, `fm` DSP ~1.1% average and 3% peak, control names from the sidecar, CV at audio rate with the orange mark. `plugins/scsynth/tests/run_on_ssp.sh` passes on the SSP. Untested: DSP of `filter` and `dfm1`, Load's error text, presets, compact editor in rack, MIDI (`midi_saw`: voices, 16-note limit, CPU with many voices).

- CV out: the `lfo` examples drive other modules from outputs 1-4 in all four script modules (user, 2026-10-10), and the voltages at the jacks match the 0.2-per-volt scaling with excellent accuracy on a voltmeter (user, 2026-10-11).

- CV on controls (`cv N`, `[cv:N]`) and its mark work on the SSP in all four script modules (user, 2026-10-10): `scsy` (`fm`), `csnd` (`quadosc.csd`), `chuk` (`sequencer.ck`), `fstr` (`osc.dsp`), each with CV on inputs 1 and 2.

- `edrm`, `strc`, `bard` and `gltc` run on the SSP (user, 2026-10-09).

- `chrs`: listed, audible, DSP ~1.3% on the SSP (2026-10-08). Untested: CV into each control, compact editor in rack, preset save and reload.

- `fstr` 0.2.0: the `tan()` fix works on the SSP; `filter.dsp` sounds, DSP ~0.9% average (2026-10-09). `chorus.dsp` ~1.3% average, 3.3% peak, as `chrs` (3.9% before the fix). Defaults apply on Load (checked 2026-10-09). Presets work; a broken `.dsp` shows its error. Untested: compact editor in rack.
