# Changelog

scsynth follows [semantic versioning](https://semver.org). Until 1.0.0, a minor version may change parameters, so presets saved with an earlier version can restore differently.

The version is set in `CMakeLists.txt` (`project(SCSYNTH VERSION ...)`) and shown at the top right of the screen.

## [Unreleased]

- scsynth's print function, which `Poll` calls on the audio thread, could allocate: the log's capacity is now reserved.

- `@buf N alloc` accepted `nan` as a length, and a length too large for the budget check overflowed it.

## [0.2.0] - 2026-10-18

Plays scores: NRT command files such as [sapf-sc](https://github.com/shakfu/sapf-sc)'s `>nrt` writes, so polyphony made of one synth per note can run on the SSP. Load picks the main synth's controls and sidecar as for a `.scsyndef`; the other synths start on schedule, and the score loops. scsynth starts them on the block holding their time, from bundles a worker thread sends 0.5 s ahead of the World's clock. See [README.md](README.md#scores).

Sample buffers: the sidecar's `// @buf N path` reads a sound file into buffer N before the synth starts, and `// @buf N alloc SECONDS [CHANNELS]` allocates an empty one for recording. A reload skips unchanged lines, so a recording survives it. Explicit numbers over a control naming the buffer: a def written for sclang, with `PlayBuf.ar(1, 0)`, works unchanged. At most 256 MB. See [README.md](README.md#buffers) and [docs/dev/scsy-buffer.md](../../docs/dev/scsy-buffer.md).

## [0.1.0] - 2026-10-10

First version, as the module `scsy`. scsynth 3.14.1 and its core UGens, and sc3-plugins 3.14.1, loaded from `/media/BOOT/scsy/ugens`. Runs a `.scsyndef` chosen with Load, or a built-in drone. Eight audio inputs and outputs. `p1`..`p16` drive the def's named controls, with ranges from a sidecar `.txt` or from the defaults; the sidecar's `cv N` adds input N as CV, at audio rate. A failed load keeps the previous def running. MIDI notes play a def with a `gate` control as voices, 16 at a time; a def without one takes `freq` and `velocity` from notes. Five examples in `examples/`: two filters, an FM voice, an LFO and a MIDI voice. See [README.md](README.md).

The last `scsy` to close unloads the UGen libraries. Synthor unloads `scsy.so` after its last module is deleted, and libscsynth's "load the UGens once" state lived in it, so each reload ran every library's load function again: `DiskIO_UGens.so` started another disk thread each time, which nothing stopped. `scripts/patches/supercollider-3.14.1-unload.patch` lets `World_New` load the UGens again after `deinitialize_library()`, which now also frees libscsynth's tables of them. The unload runs after the World's synths are freed, since their destructors live in those libraries.

Each reload of `scsy.so` also leaked 576 kB: libscsynth's FFT window and cosine tables, allocated by a static initialiser in `SC_fftlib.cpp` and never freed. The same patch frees them when the library unloads. In 20 load/unload cycles on the SSP, memory now stays within 1.5 MB of the first cycle, where it grew 0.6 MB per cycle.
