# Changelog

prdt follows [semantic versioning](https://semver.org). The version is set in `CMakeLists.txt` (`project(PRDT VERSION ...)`).

## [0.1.0] - 2026-10-31

First version. A Pure Data patch from the card, run by libpd 0.16.1 (Pd 0.56-5), vendored in `external/libpd`. Tested on the SSP.

- Jacks: In 1-8 are `[adc~ 1]`..`[adc~ 8]`, Out 1-8 `[dac~ 1]`..`[dac~ 8]`, one 64-frame Pd block late.

- Controls: a comment `@p1 cutoff 20 20000 Hz log` names encoder 1, and its value goes to `[r cutoff]`. Named receivers over fixed `[r p1]`..`[r p16]`: a patch reads as itself, and the range sits next to the name. Undeclared controls still reach `[r pN]`.

- MIDI: every channel message from the general panel's input, to `[notein]`, `[ctlin]` and the rest.

- Objects: Pd vanilla and Pd's `extra/` objects, except `pd~`, which forks a process. cyclone 0.9-5, compiled in, vendored in `external/pd-cyclone`, less `loadmess` and the three mouse objects. cyclone builds each object as its own external for one Pd instance; under one instance per module it cached symbols from the wrong instance, shared scratch buffers between modules' threads, ran `coll`'s file thread with no Pd instance, and had a use-after-free in `mtr` and leaks. All are patched; see `external/pd-cyclone/PATCHES.md`. A load fails if any object cannot be created, and the previous patch keeps running. A failure the screen shows beats a patch that plays with boxes missing.

- ELSE 1.0-0 RC-15, compiled in, vendored in `external/pd-else`: about 370 objects by their plain names and as `[else/name]`, and its abstractions in `BOOT/prdt/else`. It is upstream's, not plugdata's fork: the fork's changes are for plugdata's GUI, and it fixes none of the multi-instance faults below. cyclone keeps the ten names both libraries have (`[pink~]` is cyclone's, `[else/pink~]` ELSE's): cyclone was in first, and patches written for it keep working. Left out: `plaits~`, `sfont~`, `sfz~`, `circuit~`, the objects that need opus, aubio or Lua, and `[mouse]`, `[keycode]` and `[properties]`. `[sfload]`, `[sfinfo]` and `[play.file~]` are rewritten on Pd's own sound-file reader (`pd/else/`) rather than linking ffmpeg, which would add a large library to the SSP's build for MP3 and OGG support alone: WAV, AIFF, CAF and NeXT only. ELSE's `[play.file~]` decoded on the audio thread; here a reader thread fills a ring, as `[readsf~]` does. They enable `[sample~]`, `[player~]`, `[gran.player~]` and `[pvoc.player~]`. Under one instance per module, ELSE cached a symbol from the wrong instance, shared scratch buffers and lists between modules' threads, ran `[messcoll]`'s file thread with no Pd instance, and leaked GUI proxies and buffers on each load. `[formant~]`, `[paf~]` and `[vosim~]` wrote NaN into Pd's process-wide zero scalar, so every unconnected signal inlet in every module read NaN. All are patched; see `external/pd-else/PATCHES.md`.

- Pd fix: an abstraction made after its patch loaded, by a resized `[clone]` or dynamic patching, stayed bound to `#A` once freed, and freeing an array then read the freed memory (`external/libpd/PATCHES.md`).

- Each module has its own libpd instance (`PDINSTANCE`), so receivers, arrays and the console stay per module. The audio thread only tries Pd's process-wide class lock: it outputs one block of silence, rather than waiting, while another module creates or frees an instance.

- Memory: a load frees the previous Pd instance, including the sound buffers, path lists, symbol names and instance struct that Pd's `pdinstance_free` leaves. Unloading `prdt.so`, when the last `prdt` module is deleted, frees Pd's process-wide state through a local Pd patch, `pd_freeprocess()`; before, each unload left about 210 kB.

- Examples in `examples/`: `delay.pd`, `synth.pd` (MIDI), `cv.pd` (1 V/oct), `jacks.pd` (one jack of each kind), `midicv.pd` (MIDI to pitch CV and gate), `arp.pd`, which uses cyclone's `[counter]`, `[coll]` with embedded notes, `[cycle~]` and `[onepole~]`, and `pluck.pd`, which uses ELSE's `[rand.i]`, `[pluck~]` and `[echo.rev~]`. `make install-prdt` copies them, with Pd's `extra/` and cyclone's abstractions, to `BOOT/prdt`.
