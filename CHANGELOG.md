# Changelog

Changes to the build, tools and presets. Each plugin keeps its own changelog in `plugins/<name>/CHANGELOG.md`.

## [0.2.0] - 2026-10-09

First release of all the modules together, as `shakfu-ssp-plugins-0.2.0.zip`, tagged `0.2.0`. 0.1.0 released `sfct` alone.

### Plugins

- `chorus` (`chrs`), new: a stereo chorus compiled from Faust ahead of time. See [plugins/chorus/CHANGELOG.md](plugins/chorus/CHANGELOG.md).

- `faust` (`fstr`), new: Faust programs loaded from the card and compiled on the SSP by libfaust's LLVM JIT. See [plugins/faust/CHANGELOG.md](plugins/faust/CHANGELOG.md).

- `rack` 0.2.0, its first release as rack: presets on the BOOT partition, a full-screen routing view, coarse level steps, and limits on module inputs that fixed a crash. See [plugins/rack/CHANGELOG.md](plugins/rack/CHANGELOG.md).

### Shared code

- `plugins/common/engine`: `FaustEngine.h` and `FaustProcessor.*` host a compiled Faust kernel as an engine plugin.

- `ScriptEngine` takes its controls from the compiler through `declared()` as well as from `@pN` comments. A program chosen with Load sets controls that declare a default; `csnd` and `chuk` declare none and are unchanged. `status()` adds lines to the status panel.

- `MiniBasicView::showButtonBox` lets a view hide its soft key labels.

### Build

- `scripts/build_deps.sh` builds libfaust 2.85.9 with the LLVM and interpreter backends: against the buildroot's LLVM 9.0.1 for the SSP, whose rootfs has the same `libLLVM-9.so`, and the system LLVM for the host. `llvm-dev` joins the host packages.

- `scripts/patches/faust-2.85.9-jit-target.patch`: libfaust built its JIT before storing the requested target, so on the SSP it compiled ARMv4 soft-float code and `tan()` returned garbage. Upstream fixed the target in 2.88.0; the matching `opt_level` bug is drafted as [docs/dev/issues/261008-jit-target.md](docs/dev/issues/261008-jit-target.md).

- `make faust-kernels` regenerates committed Faust kernel headers with `scripts/faust_kernel.sh` (cyfaust through `uv`).

- `make install-faust` copies the Faust libraries and the `fstr` examples to `BOOT/faust`.

- `plugins/common/CMakeLists.txt` and `engine/CMakeLists.txt` name their sources from their own directory, not `../common`, so a plugin builds from any folder.

- `examples/svca` and `examples/tremolo`, the plugin guides' worked examples, are built and tested on the host only; a release does not include them.

- `scripts/new_plugin.py DIR NAME [--faust]` copies one of them to `plugins/DIR` as module NAME: renamed, with README and CHANGELOG stubs, added to `plugins/CMakeLists.txt` and, for Faust, to `make faust-kernels`. It refuses a name that is not four characters or that another plugin uses. A Faust copy renames the committed kernel header, which matches a regenerated one, so the new plugin builds without cyfaust.

- `make install-presets` copies to `BOOT/rack_presets` and needs no `sudo`; rack presets were on the root-owned ext4 partition.

### Release

- `make release` packages `releases/shakfu-ssp-plugins-<VERSION>/` and its zip; the git tag is the bare version, as 0.1.0's was. `BOOT/` is laid out as the SD card, so copying it onto the card's BOOT partition installs every module and the data it reads. A generated README lists every module; `docs/` holds each one's README and changelog, with links into the repo pointing at the release tag. The version is in `VERSION`. It replaces `scripts/release.ssp.sh`, which zipped the plugins with TheTechnobear's 2022 note on his Synthor build.

- `make release-pdf` packages the same, with the docs rendered to PDF by quarto.

- `make release-notes` writes `CHANGELOG.md`'s section for `VERSION` as the GitHub release body, with links pointing at the tag; `make release` runs it last. Changelog headings follow Keep a Changelog: `## [0.2.0] - 2026-10-09`.

- `make publish` creates the GitHub release with `gh`, attaching the zip and the notes. It refuses unless the tree is clean, the tag is `HEAD` and pushed, the zip is newer than `HEAD`, and no release exists. It never commits, tags or pushes.

- `make clean-releases` deletes `releases/`, every version's package, zip and notes. `make release` replaces only its own version's files, so earlier releases stay until then.

### Tools

- `plugin_host` gains `channels`, `load`, `button`, `encoder` and `render`. `render` draws a plugin's editor to a 1600 x 480 image, so a view can be checked on the host.

- `tools/py2rack/modules-local.json` lists this repo's modules, read from the built plugins; a test fails when it is stale (`UPDATE_MANIFEST=1 make test`). `py2rack encode -m` can be given more than once.

- `plugins/faust/bench/faust_bench.cpp` times one `.dsp` compiled, JIT-compiled and interpreted.

### Presets

- Nine new rack presets: `euclid_drums`, `bass_line`, `ambient_rings`, `glitch_chorus`, `radio_drift`, `dub_delay`, `harmonic_drone`, `jam_four_tracks` and `empty`. See [presets/README.md](presets/README.md).

- `make test` parses every preset's matrices with rack's own loader.

### Docs

- `README.md` and the release README credit [sk-engines](https://github.com/shakfu/sk-engines), the source of `rdio`, `csnd`, `chuk`, `edrm`, `strc`, `bard` and `gltc`, and of `chrs`'s DSP. Each module's README said so; the top-level ones did not. The release README's module table gains a From column, and `README.md`'s Origins section lists every source.

- [docs/dev/faust.md](docs/dev/faust.md): the four routes from Faust to an SSP module, the interpreter and JIT benchmarks, and the decisions behind `chrs` and `fstr`.

- [docs/dev/faust-jit-tan.md](docs/dev/faust-jit-tan.md): the investigation of `tan()` in JIT code on the SSP.

- [docs/DEVELOPING.md](docs/DEVELOPING.md): the shared code and tools, and which route to take for a new module. [docs/CPP_PLUGINS.md](docs/CPP_PLUGINS.md) and [docs/FAUST_PLUGINS.md](docs/FAUST_PLUGINS.md) build one step by step: a C++ plugin, and a Faust program as a compiled module or as an `fstr` program.

- The `csnd`, `chuk` and `fstr` READMEs walk through one example each, from the card to the jacks, encoders and status panel. `fstr`'s builds `examples/filter_cv.dsp`, `filter.dsp` with cutoff CV on input 3, which now ships with the other examples.

- Every port's README links its engine's folder in sk-engines, and mentions of [cyfaust](https://github.com/shakfu/cyfaust) and [daisy-apps](https://github.com/shakfu/daisy-apps) link the projects.
