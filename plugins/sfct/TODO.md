# sfct TODO

## Measure on the SSP

These decide the open questions below.

- [ ] Worst-case CPU: all eight voices recording, loops about 0.5 s. Read the DSP average and peak. So far: 4 voices 10% (before the perf patch); 8 voices in `norns` mode 10%, peak about 20%.

- [ ] Free memory: `MemAvailable` with a typical patch, without sfct and with sfct in `4 loop` after a load.

- [ ] Install: whether a restart is needed after copying `sfct.so`, and which category Synthor lists it under.

- [ ] Version: Synthor accepts `0.1.0` in the descriptor (it was a build date), and saved patches keep sfct.

## Verify on the SSP

Built and tested natively where possible, but not yet run on the device:

- [ ] CV inputs: Rate, Pos, Rec gate, Cut trigger.

- [ ] Feedback, including saturation near amount 1 with high pre level.

- [ ] Clear (hold RS, labelled CLR): a loop in a track, everything in the Global view. RS was previously never drawn, so check the label shows. Freeing of swapped-out buffers after loads.

- [ ] Load, record, save and preset restore, in both modes.

- [x] `4 loop` mode works on the SSP (user, 2026-10-21).

- [ ] Record once (hold rec).

- [x] Track input routing (Global page 2): a mono source on In L recorded to both sides with `L`.

- [ ] Sync from MIDI clock; MIDI Start with transport enabled.

- [ ] Phase outputs.

- [ ] Filter and fade pages.

- [ ] `norns` mode: three tracks, TRK+ from track 3 to the Global view, track 4 silent. Held EN-/EN+ step one voice (1 L, 1 R, 2 L, ...) at the same page.

- [ ] A mono file on a linked track fills L and R; unlinked, only the voice's buffer.

- [ ] Judge by ear: `Pos` at 1 s/V; synced loop length in buffer time, so rate 0.5 doubles it in beats.

## Open

- [x] Metadata: shakfu, manufacturer code `SF00`, in `CMakeLists.txt`. `PLUGIN_CODE SFCT` is unchanged: the descriptor's uid comes from it. Check on the SSP that saved patches still find sfct.

- [ ] Buffer length: held at 2^21 frames (43.7 s) until memory and CPU are measured. Longer buffers also need start/end encoder steps scaled to the buffer.

- [ ] Compact UI: no Load, Save, Clear, record once or waveform; 8 pages per voice is a long scroll.

- [ ] Tests above the engine: linking, preset-restore guard, CV reading, WAV IO, mode restore. Needs a native JUCE test build.

- [ ] Copy one buffer to another, e.g. L to R, including recordings: a mono take or file on both sides without reloading. Needs a gesture (every button is assigned) and a copy on the audio thread in slices, as Clear does. The copy is not in presets until saved.

- [ ] 1.0.0 once the device checks pass.

## Optional

- [ ] Skip the SVFs when their mixes are 0: about 12% less CPU on x86, but not byte-identical when a filter resumes.

- [ ] Sample-accurate CV, cut triggers and phase pulses (now per 2.7 ms block).

- [ ] Band-limited resampling on load (Lagrange aliases when downsampling).

## Housekeeping

- [ ] Upstream `external/softcut-lib/patches/sfct-perf.patch` to softcut-py.

## Done

0.1.0: eight voices in four tracks, `norns` and `4 loop` modes, linking, track input routing, waveform with crossfade curves, load/save, clear a loop or everything, CV, feedback, full softcut controls, record once, MIDI clock sync, phase outputs, the perf patch, metadata, user guide and dev notes. See CHANGELOG.md.

Confirmed on the SSP: modes, navigation, rate steps, EN paging across a track, hold navigation (TRK, EN), fade curve drawing, input routing, CPU with all voices on (10% average, 20% peak).
