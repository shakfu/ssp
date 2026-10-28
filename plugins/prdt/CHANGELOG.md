# Changelog

prdt follows [semantic versioning](https://semver.org). The version is set in `CMakeLists.txt` (`project(PRDT VERSION ...)`).

## [Unreleased]

First version. A Pure Data patch from the card, run by libpd 0.16.1 (Pd 0.56-5), vendored in `external/libpd`. Tested on the SSP.

- Jacks: In 1-8 are `[adc~ 1]`..`[adc~ 8]`, Out 1-8 `[dac~ 1]`..`[dac~ 8]`, one 64-frame Pd block late.

- Controls: a comment `@p1 cutoff 20 20000 Hz log` names encoder 1, and its value goes to `[r cutoff]`. Named receivers over fixed `[r p1]`..`[r p16]`: a patch reads as itself, and the range sits next to the name. Undeclared controls still reach `[r pN]`.

- MIDI: every channel message from the general panel's input, to `[notein]`, `[ctlin]` and the rest.

- Objects: Pd vanilla and Pd's `extra/` objects, except `pd~`, which forks a process. cyclone 0.9-5, compiled in, vendored in `external/pd-cyclone`, less `loadmess` and the three mouse objects. cyclone builds each object as its own external for one Pd instance; under one instance per module it cached symbols from the wrong instance, shared scratch buffers between modules' threads, ran `coll`'s file thread with no Pd instance, and had a use-after-free in `mtr` and leaks. All are patched; see `external/pd-cyclone/PATCHES.md`. A load fails if any object cannot be created, and the previous patch keeps running. A failure the screen shows beats a patch that plays with boxes missing.

- Each module has its own libpd instance (`PDINSTANCE`), so receivers, arrays and the console stay per module. The audio thread only tries Pd's process-wide class lock: it outputs one block of silence, rather than waiting, while another module creates or frees an instance.

- Memory: a load frees the previous Pd instance, including the sound buffers, path lists, symbol names and instance struct that Pd's `pdinstance_free` leaves. Unloading `prdt.so`, when the last `prdt` module is deleted, frees Pd's process-wide state through a local Pd patch, `pd_freeprocess()`; before, each unload left about 210 kB.

- Examples in `examples/`: `delay.pd`, `synth.pd` (MIDI), `cv.pd` (1 V/oct) and `arp.pd`, which uses cyclone's `[counter]`, `[coll]` with embedded notes, `[cycle~]` and `[onepole~]`. `make install-prdt` copies them, with Pd's `extra/` and cyclone's abstractions, to `BOOT/prdt`.
