# TODO

Design and source facts: [docs/dev/libpd.md](../../docs/dev/libpd.md).

## High

- [x] Engine on the SSP's CPU (`tests/run_on_ssp.sh`, 2026-10-28): engine and example tests, the thread test 20 times on the isolated cores, DSP time per block (examples under 1%, `rev3~` 3%).
- [x] Unload loop on the SSP (2026-10-28): reloads, instances and `prdt.so` unloads flat, the last with `pd_freeprocess()`.
- [x] In Synthor (user, 2026-10-28): the built-in, `delay.pd` with audio and encoders, `synth.pd` from a MIDI controller, two modules at once, a preset restore.

## Medium

- [ ] MIDI out: libpd's hooks to the general panel's MIDI output, through a queue to the worker.
- [ ] A full-page console. `ScriptEditor` shows `output()` only in the Code view, which needs `CodeKeys`.

## Low

- [x] cyclone 0.9-5 (2026-10-28): host tests, the engine on the SSP's CPU, and `arp.pd` in Synthor with `prdt.so` unloaded and loaded again (user).
- [x] ELSE 1.0-0 RC-15 (2026-10-29): host tests, release, TSan and ASan, every object and abstraction created and freed.
- [x] ELSE on the SSP's CPU (2026-10-29): `tests/run_on_ssp.sh`, the thread test 20 times, bench unchanged.
- [x] ELSE in Synthor (user, 2026-10-29): `pluck.pd` loaded twice, and `prdt.so` unloaded and loaded again.
- [ ] ELSE's `[sfload]`, `[sfinfo]` and `[play.file~]` on Pd's own sound-file code (WAV, AIFF, CAF), not ffmpeg, which is not worth linking: `[sfload]` through `soundfiler`'s reader, `[sfinfo]` from the header alone, `[play.file~]` through `readsf~`'s threaded reader. This would enable `[sample~]`, `[player~]`, `[gran.player~]` and `[pvoc.player~]` (remove them from `ELSE_UNSUPPORTED` in `tests/test_prdt.py`). Keep each object's messages and outlets, and say which ones a file format without ffmpeg cannot serve, such as MP3 or OGG files.
