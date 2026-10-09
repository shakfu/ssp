# Changelog

Changes to the build, tools and presets. Each plugin keeps its own changelog in `plugins/<name>/CHANGELOG.md`.

## Unreleased

### Plugins

- `chorus` (`chrs`), new: a stereo chorus compiled from Faust ahead of time. See [plugins/chorus/CHANGELOG.md](plugins/chorus/CHANGELOG.md).

- `faust` (`fstr`), new: Faust programs loaded from the card and compiled on the SSP by libfaust's LLVM JIT. See [plugins/faust/CHANGELOG.md](plugins/faust/CHANGELOG.md).

- `rack`: presets on the BOOT partition, a full-screen routing view, coarse level steps, and limits on module inputs that fixed a crash. See [plugins/rack/CHANGELOG.md](plugins/rack/CHANGELOG.md).

### Shared code

- `plugins/common/engine`: `FaustEngine.h` and `FaustProcessor.*` host a compiled Faust kernel as an engine plugin.

- `ScriptEngine` takes its controls from the compiler through `declared()` as well as from `@pN` comments. A program chosen with Load sets controls that declare a default; `csnd` and `chuk` declare none and are unchanged. `status()` adds lines to the status panel.

- `MiniBasicView::showButtonBox` lets a view hide its soft key labels.

### Build

- `scripts/build_deps.sh` builds libfaust 2.85.9 with the LLVM and interpreter backends: against the buildroot's LLVM 9.0.1 for the SSP, whose rootfs has the same `libLLVM-9.so`, and the system LLVM for the host. `llvm-dev` joins the host packages.

- `scripts/patches/faust-2.85.9-jit-target.patch`: libfaust built its JIT before storing the requested target, so on the SSP it compiled ARMv4 soft-float code and `tan()` returned garbage. Upstream fixed the target in 2.88.0; the matching `opt_level` bug is drafted as [docs/dev/issues/261008-jit-target.md](docs/dev/issues/261008-jit-target.md).

- `make faust-kernels` regenerates committed Faust kernel headers with `scripts/faust_kernel.sh` (cyfaust through `uv`).

- `make install-faust` copies the Faust libraries and the `fstr` examples to `BOOT/faust`.

- `make install-presets` copies to `BOOT/rack_presets` and needs no `sudo`; rack presets were on the root-owned ext4 partition.

### Tools

- `plugin_host` gains `channels`, `load`, `button`, `encoder` and `render`. `render` draws a plugin's editor to a 1600 x 480 image, so a view can be checked on the host.

- `tools/py2rack/modules-local.json` lists this repo's modules, read from the built plugins; a test fails when it is stale (`UPDATE_MANIFEST=1 make test`). `py2rack encode -m` can be given more than once.

- `plugins/faust/bench/faust_bench.cpp` times one `.dsp` compiled, JIT-compiled and interpreted.

### Presets

- Nine new rack presets: `euclid_drums`, `bass_line`, `ambient_rings`, `glitch_chorus`, `radio_drift`, `dub_delay`, `harmonic_drone`, `jam_four_tracks` and `empty`. See [presets/README.md](presets/README.md).

- `make test` parses every preset's matrices with rack's own loader.

### Docs

- [docs/dev/faust.md](docs/dev/faust.md): the four routes from Faust to an SSP module, the interpreter and JIT benchmarks, and the decisions behind `chrs` and `fstr`.

- [docs/dev/faust-jit-tan.md](docs/dev/faust-jit-tan.md): the investigation of `tan()` in JIT code on the SSP.
