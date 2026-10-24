# Changelog

Changes to the build, tools and presets. Each plugin keeps its own changelog in `plugins/<name>/CHANGELOG.md`.

## [0.5.0]

One module is new since 0.4.0: `gseq`, so the release ships 19.

### Plugins

- `gseq`, new: a grid sequencer of 8 rows of up to 32 steps, each row with its own length and division, out as MIDI and CV voices with velocity CV. Recording from a MIDI keyboard, step entry, and a piano-roll mode with chords. A Launchpad Mini MK3 or Launchpad X edits and shows the grid; the X's pads send velocity. See [plugins/gseq/CHANGELOG.md](plugins/gseq/CHANGELOG.md).

- `sapf` moves to the script editor of `chuk`, `csnd`, `fstr` and `alda`, and its mouse words read encoders 1 and 2. See [plugins/sapf/CHANGELOG.md](plugins/sapf/CHANGELOG.md).

- `BaseProcessor` guards its MIDI automation map with a lock. The MIDI input thread iterated it while a preset load cleared it, or an editor erased an entry: a use-after-free. Its MIDI echo of a learned parameter now takes the device lock, as `sendMidiMessagesNow` does since 0.4.0; it tries rather than waits, since a device change holding the lock stops the MIDI input thread that may be the caller.

- A preset's MIDI automation naming a parameter index the module lacks is dropped on load. The MIDI system page drew it through a null parameter and crashed.

- The MIDI system page's output list compared outputs by the identifiers of the input list.

- `rack`, `sfct`, `alda`, `csnd`, `scsy`, `bard`, `ttyp`, `fstr` and `edrm` fixes: see their CHANGELOGs.

- Docs: [docs/USING.md](docs/USING.md) covers what every module shares: installing, card paths, the rule that Synthor runs a module only while a jack is patched, buttons, the general panel and USB hubs. [docs/script-editor.md](docs/script-editor.md) covers the code editor, keys, controls and CV the script modules share. Each module README now opens with Install and First use, is titled by its module name, and numbers its jacks. The root README opens with installing and a module table; the release ships both shared docs.

### Build and tools

- The `install-*` targets copy over ssh to `SSP_HOST` when the card is not mounted locally, and check each copy by md5 (`scripts/ssp-card.sh`). The card kept a `sapf-examples.txt` with errors that the fork had fixed, as the scripts needed the card in the desktop. `make install` still needs the mounted card; `make deploy` is its ssh form.

- `scripts/ssp_card_image.py` images an SSP card to a compressed file and writes it to another card. It copies the whole device, as the rk3288 boots from loader sectors outside the partitions, but stops at the last partition's end, so the image fits a card slightly smaller than the source.

- `SSP_IP` sets the SSP's address in the Makefile, and `SSP_HOST` defaults to `root@$(SSP_IP)`. `midisend` and `mididump` had the address written in.

- `make deploy` syncs the card after copying. A copy by hand without `sync`, then a power-cycle, left a half-written `edrm.so` on the card, which Synthor loaded; `make deploy` had the same gap. `make deploy-mod` already synced.

- `sfct` joins the plugins `plugins/common/tests` drives through the SSP API, and `tools/py2rack/modules-local.json`.

- `make mididump` (`tools/mididump`): prints the notes and controllers of the SSP's MIDI ports whose names contain a word, over ssh. The SSP has no alsa-utils; it found that a stuck note during the `gseq` Launchpad test came from Plaits droning, not from a missing note-off.

- `plugins/common/launchpad`: the Programmer-mode protocol of the Launchpad Mini MK3 and Launchpad X, and a JUCE device that finds, opens and reconnects either, preferring the X; `gseq` uses it.

## [0.4.0]

Five modules are new since 0.3.0: `dexd`, `sapf`, `alda`, `fsyn` and `brks`, so the release ships 18.

### Plugins

- `brks`, new: the breakbeat slicer firmware of [_core](https://github.com/schollz/_core) v8.1.0, built through `_core`'s desktop engine with one hook added to its firmware for the bank step. It plays sample-manager card folders, with the panel's knobs, Tap, Mode, Play, Tempo, Clock Out, a clock input and three CVs; Left and Right (PG-/PG+) change bank. `scripts/fetch_core.sh` fetches `_core` and generates the engine per target (`make deps`). `make install-brks` copies `_core`'s default banks to the card. On the SSP the engine is compiled Thumb-2: clang 18 and 20 miscompile it in ARM mode. GPL-3.0-only. See [plugins/breaks/CHANGELOG.md](plugins/breaks/CHANGELOG.md) and [docs/dev/breaks.md](docs/dev/breaks.md).

- `sapf`, new: sapf, through the fork [shakfu/sapf](https://github.com/shakfu/sapf), for live coding from a USB keyboard or over ssh. A thread of its own renders 4 blocks (10.7 ms) ahead of the host's callback, so a late render silences sapf and not the patch. Eight inputs and eight outputs. `scripts/fetch_sapf.sh` fetches the fork (`make deps`), and `make install-sapf` copies its prelude and examples to the card. GPL-3.0. See [plugins/sapf/CHANGELOG.md](plugins/sapf/CHANGELOG.md).

- `ScriptEngine` (`csnd`, `chuk`, `fstr`): a preset saved while a program compiled named the previous program. `idle()` dequeued the load, but `path()` and `code()` read the program from the running slot, which changed only after the compile. Restoring that preset ran the old program. `test_chuck` failed about one run in three: its second `set` saved during the first compile and reloaded the built-in. The program now becomes current when it is dequeued.

- `alda`, new: an Alda score plays in a loop as gates and 1 V/octave pitch CV on four mono voices, and as MIDI on every part through an ALSA port of its own, which `dexd` and other modules choose as MIDI IN. It follows the score's tempo, a clock on In 2, or MIDI clock. Its controls are Transpose, Tempo, Velocity, Swing, a mute per voice, and Clock: free, analog, MIDI, or the score's `# @clock` line. It embeds psnd's C implementation of Alda, fetched by `scripts/fetch_alda.sh` (`make deps`). GPL-3.0. See [plugins/alda/CHANGELOG.md](plugins/alda/CHANGELOG.md) and [docs/dev/dsl-alda.md](docs/dev/dsl-alda.md).

- `fsyn`, new: FluidSynth 2.6.1 plays SF2 soundfonts from MIDI in stereo, a General MIDI synth for `alda`'s port. It ships FluidR3_GM (148 MB, MIT): of the three GM fonts tried on the SSP it sounds best at the same DSP cost, 4-5% playing `alda` scores. A font loads into a second synth on the worker thread and swaps in: `fluid_synth_sfload` holds the synth's lock for the whole read, 2.4 s for the 148 MB FluidR3_GM on the SSP, which would stall MIDI fed from the audio thread. SF2 only; SF3 needs libsndfile with Ogg/Vorbis. `scripts/build_fluidsynth.sh` builds FluidSynth (`make deps`), and `make install-fsyn` copies the font to the card. See [plugins/fsyn/README.md](plugins/fsyn/README.md).

- Code view, in `csnd`, `chuk`, `fstr` and `alda`:
  - Ctrl-Shift-S saves the text under a name typed in a prompt, beside the current file. That file becomes the program's, so Ctrl-S writes there next. An existing file is overwritten only on a second Enter.

  - Ctrl-L empties the view; pressed again on the empty view, it brings the text back, as there is no undo.

  - Ctrl-R stops, empties and forgets the program file, where the engine runs an empty program as silence (`ScriptEngine::canReset`): `alda` only. Csound rejects an empty program and keeps the last.

  - The status line lists the module's own keys (`CodeKeys::hint`) and can show the engine's state in place of `running` (`ScriptEngine::runState`). It is shorter (`keys usb` is now `usb`): the view clips text at about x=1530, and the longest key list ran past it.

- `ScriptProcessor` takes a plugin's own jack names, and an engine can declare its own controls (`ScriptEngine::declare`), including ones that pick from named values (`ParamSpec::choices`), one per encoder click. Other plugins keep `In N`, `Out N` and their `@pN` comments. For `alda`.

- `dexed` (`dexd`), new: a 6-operator FM synth played from MIDI, on the Dexed engine through Synth_Dexed, with DX7 cartridges from the card. GPL-3.0. See [plugins/dexed/CHANGELOG.md](plugins/dexed/CHANGELOG.md) and [docs/dev/dexed.md](docs/dev/dexed.md).

- Every `EngineEditor` module ends its DSP readout before the version label. The readout was right-aligned to x=1590 and drew over the label, which starts at x=1505.

- `fstr`, `csnd` and `chuk` edit their program from a keyboard, as `sapf` does. `ScriptProcessor` provides it for plugins that pass it a key port; `scsy` passes none, since its programs are binary SynthDefs. `TextBuffer` and `KeyQueue` move from `plugins/sapf` to `plugins/common/keys`, along with sapf's code drawing (`drawCode`). `TextBuffer::block` takes the language's comment marker: with sapf's `;`, a ChucK line such as `0 => int i; while (i < 4) {` ended its bracket count at the `;`. `ScriptEngine` runs Ctrl-E blocks through engine overrides; `chuk` and `csnd` implement them.

- The code view can colour code from a tree-sitter grammar: `CodeKeys` takes the grammar and its `highlights.scm`, and `Highlighter` applies the query. `chuk`, `csnd`, `fstr`, `sapf` and `alda` use it; `ssp_highlights()` adds a grammar to a plugin. The runtime (v0.27.0) and the ChucK, Faust, Csound and Alda grammars are vendored in `external/`; sapf's is new, a token grammar after sapf's own lexer. The Csound grammar, from psnd, gained two fixes: section tags other than `<CsoundSynthesizer>`, and one-character tokens and spaces that its catch-all token took in. See [docs/dev/syntax-highlighting.md](docs/dev/syntax-highlighting.md).

- `EngineEditor` passes a short Left or Right press to the module through `step(dir)`; long presses still move between page groups. `dexd` uses it for PG- and PG+.

### Build and tools

- `scripts/build_fluidsynth.sh ssp` links GCC 8's `libstdc++fs.a`, as does `fsyn`. Without it FluidSynth's `std::filesystem` check fails to link in the cross build, its file tests become stubs, and every soundfont load logs `fluid_file_test is unavailable`, which `fsyn` showed as the load's error.

- `BaseProcessor::sendMidiMessagesNow` takes the device lock and returns false without a device; it dereferenced `midiOutDevice_` unchecked, so a send after a disconnect, or during the async check's reconnection, crashed. `releaseResources` takes the lock too. No plugin called it before `alda`.

- `midisend` also sends controllers, pitch bend, program change and channel aftertouch: `cc N V`, `bend V`, `pc N`, `at V`.

- `BaseProcessor::midiMessageInput` gives a plugin every channel message on its MIDI channel: pitch bend, controllers, aftertouch, program change, and notes while Note In is set. Before, plugins got only notes. It sits beside `midiNoteInput` rather than replacing it, so existing plugins are unchanged. `plugins/common/tests/midi` tests the dispatch on the host. For `dexd`; see [docs/dev/dexed.md](docs/dev/dexed.md).

## [0.3.0]

0.2.0 was prepared but never tagged or published, so 0.3.0 is the first release since 0.1.0. It contains the changes here and everything in the 0.2.0 section of [CHANGELOG.md](CHANGELOG.md): 0.1.0 released `sfct` alone, so every other module is new since then.

### Plugins

- `teletype` (`ttyp`), new: monome teletype 5, live-coded from a USB keyboard, with scene files on the card. GPL-3.0. See [plugins/teletype/CHANGELOG.md](plugins/teletype/CHANGELOG.md) and [README-getting-started.md](plugins/teletype/README-getting-started.md).

### Build and tools

- `make deps` also runs `scripts/fetch_teletype.sh`, which fetches the teletype sources at pinned, SHA-256-checked commits, and builds ragel if it is not installed. `ttyp` is configured only once they exist.

- `tools/ttyp_type.py` types text into `ttyp` through its key port.

- `plugin_host`'s `enable CH` marks an input as patched, as Synthor does.

- The release package puts `ttyp`'s example scenes in `BOOT/teletype/examples` and lists its licence.

## [0.2.0]

### Plugins

- `chuck` (`chuk`), new: ChucK programs loaded from the card, with CV and MIDI notes from the general panel's input. See [plugins/chuck/CHANGELOG.md](plugins/chuck/CHANGELOG.md).

- `csound` (`csnd`), new: Csound orchestras loaded from the card, with CV and MIDI notes. See [plugins/csound/CHANGELOG.md](plugins/csound/CHANGELOG.md).

- `chorus` (`chrs`), new: a stereo chorus compiled from Faust ahead of time. See [plugins/chorus/CHANGELOG.md](plugins/chorus/CHANGELOG.md).

- `faust` (`fstr`), new: Faust programs loaded from the card and compiled on the SSP by libfaust's LLVM JIT. See [plugins/faust/CHANGELOG.md](plugins/faust/CHANGELOG.md).

- `rack` 0.2.0, its first release as rack: presets on the BOOT partition, a full-screen routing view, coarse level steps, and limits on module inputs that fixed a crash. See [plugins/rack/CHANGELOG.md](plugins/rack/CHANGELOG.md).

- `scsynth` (`scsy`), new: SuperCollider SynthDefs run in scsynth 3.14.1, with sc3-plugins. See [plugins/scsynth/CHANGELOG.md](plugins/scsynth/CHANGELOG.md).

### Shared code

- `BaseProcessor`'s MIDI thread was detached and given 100 ms to stop. If it overran, it read the freed processor, or ran code from a `.so` Synthor had unloaded. It is now joined.

- `BaseProcessor`'s MIDI device lock tested `atomic_flag::test_and_set()` inverted: a device change from the panel and the async device check each went ahead when the other held the lock, and the check skipped every other pass. A `std::mutex` replaces it; the check still backs off when the lock is busy.

- `plugins/common/engine`: `FaustEngine.h` and `FaustProcessor.*` host a compiled Faust kernel as an engine plugin.

- `ScriptEngine` takes its controls from the compiler through `declared()` as well as from `@pN` comments. A program chosen with Load sets controls that declare a default; `csnd` and `chuk` declare none and are unchanged. `status()` adds lines to the status panel.

- `MiniBasicView::showButtonBox` lets a view hide its soft key labels.

- `ScriptEngine::readProgram()` reads a program file; an engine with a binary format overrides it. By default it is `readText`, which drops CR bytes.

- `cv N` on an `@pN` line, and `[cv:N]` in Faust, add input N to a control as CV in `csnd`, `chuk`, `fstr` and `scsy`: once per 128-frame block in `csnd` and `fstr`, every 10 ms in `chuk`, at audio rate in `scsy`. A control's knob sets the value its CV moves from. An orange mark on the control's bar, in the full and compact editors, shows where the CV has moved it, and the value shown is the moved one: a parameter that implements `ssp::ModulatedParameter` reports the moved value to the controls.

### Build

- `scripts/build_deps.sh` builds libfaust 2.85.9 with the LLVM and interpreter backends: against the buildroot's LLVM 9.0.1 for the SSP, whose rootfs has the same `libLLVM-9.so`, and the system LLVM for the host. `llvm-dev` joins the host packages.

- `scripts/patches/faust-2.85.9-jit-target.patch`: libfaust built its JIT before storing the requested target, so on the SSP it compiled ARMv4 soft-float code and `tan()` returned garbage. Upstream fixed the target in 2.88.0; the matching `opt_level` bug is drafted as [docs/dev/issues/261008-jit-target.md](docs/dev/issues/261008-jit-target.md).

- `make faust-kernels` regenerates committed Faust kernel headers with `scripts/faust_kernel.sh` (cyfaust through `uv`).

- `make install-faust` copies the Faust libraries and the `fstr` examples to `BOOT/faust`.

- `plugins/common/CMakeLists.txt` and `engine/CMakeLists.txt` name their sources from their own directory, not `../common`, so a plugin builds from any folder.

- `examples/svca` and `examples/tremolo`, the plugin guides' worked examples, are built and tested on the host only; a release does not include them.

- `scripts/new_plugin.py DIR NAME [--faust]` copies one of them to `plugins/DIR` as module NAME: renamed, with README and CHANGELOG stubs, added to `plugins/CMakeLists.txt` and, for Faust, to `make faust-kernels`. It refuses a name that is not four characters or that another plugin uses. A Faust copy renames the committed kernel header, which matches a regenerated one, so the new plugin builds without cyfaust.

- `make install-presets` copies to `BOOT/rack_presets` and needs no `sudo`; rack presets were on the root-owned ext4 partition.

- `scripts/build_deps.sh` builds libscsynth, SuperCollider's core UGens, FFTW and sc3-plugins, at one version (`SC_REF`), and patches four data races in scsynth (`scripts/patches/supercollider-3.14.1-threads.patch`). `make install-scsy` copies the UGens and the `scsy` examples to the card.

- `scripts/build_deps.sh` builds ChucK without `KBHit` and `Hid` (`__DISABLE_KBHIT__`, `__DISABLE_HID__`, `__ALTER_HID__`), whose process-wide threads outlived `chuk.so`. It records ChucK's defines in `build/chuck/.defs` and rebuilds ChucK when they change: `make` does not track flags, so new defines left the old objects in `libchuck.a`.

- JUCE builds from a patched copy in the build directory, so the `juce` submodule stays at its commit. `scripts/patches/juce-8.0.10-thread-local.patch` replaces two `thread_local` caches in JUCE's text shaping (`juce_Unicode.cpp`, `juce_UnicodeBidi.cpp`). Their destructors were registered on Synthor's main thread, which never exits, and glibc does not unload a library with pending `thread_local` destructors: any plugin whose editor shaped text stayed loaded after its last module was deleted.

- Each engine's tests check that no thread outlives the engine, the failure that crashed Synthor when it unloaded a plugin.

- `scripts/patches/supercollider-3.14.1-unload.patch`: libscsynth can unload its UGen libraries and load them again in the same process. `scsy` uses it when its last World closes, so no UGen library's thread outlives `scsy.so`. It also frees libscsynth's FFT tables at unload, which leaked 576 kB per reload.

- `make midisend` starts `tools/midisend` on the SSP: an ALSA sequencer port that a module can choose as its MIDI input, and that plays notes written to a FIFO over ssh. It tests MIDI without a keyboard.

- `SSP_HOST` defaults to `root@192.168.1.6`, the SSP's address with a USB-Ethernet adapter; see `docs/BUILDING.md`.

### Known issues

- Each `chuk` instance leaks about 2.9 MB when deleted: ChucK does not free a VM's type system. `fstr` leaks about 0.06 MB per load and unload. See [docs/dev/cleanup-problems.md](docs/dev/cleanup-problems.md).

- Once, a reloaded `fstr.so` crashed Synthor inside LLVM's JIT (`MCJIT::notifyObjectLoaded`): 1 crash in 12 cycles, not reproduced since. See [docs/dev/cleanup-problems.md](docs/dev/cleanup-problems.md).

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

- `scripts/ssp-memlog.sh` logs Synthor's memory, threads and open files once a second on the SSP. `scripts/ssp-crash.gdb` attaches to Synthor and records a crash: Synthor's stderr is `/dev/null`, and gdb on the SSP cannot unwind past libc. It stops reading symbols for libraries loaded after it attaches, which made each `scsy` add and delete take about 9 s, and reads them all on a crash.

### Presets

- Nine new rack presets: `euclid_drums`, `bass_line`, `ambient_rings`, `glitch_chorus`, `radio_drift`, `dub_delay`, `harmonic_drone`, `jam_four_tracks` and `empty`. See [presets/README.md](presets/README.md).

- `make test` parses every preset's matrices with rack's own loader.

### Docs

- `README.md` and the release README credit [sk-engines](https://github.com/shakfu/sk-engines), the source of `rdio`, `csnd`, `chuk`, `edrm`, `strc`, `bard` and `gltc`, and of `chrs`'s DSP. Each module's README said so; the top-level ones did not. The release README's module table gains a From column, and `README.md`'s Origins section lists every source.

- [docs/dev/faust.md](docs/dev/faust.md): the four routes from Faust to an SSP module, the interpreter and JIT benchmarks, and the decisions behind `chrs` and `fstr`.

- [docs/dev/faust-jit-tan.md](docs/dev/faust-jit-tan.md): the investigation of `tan()` in JIT code on the SSP.

- [docs/dev/midi-review.md](docs/dev/midi-review.md): the device test of MIDI in the four script modules, and the crashes found during it: `fstr` under flush-to-zero, threads outliving their `.so`, and the MIDI device lock.

- [docs/DEVELOPING.md](docs/DEVELOPING.md): the shared code and tools, and which route to take for a new module. [docs/CPP_PLUGINS.md](docs/CPP_PLUGINS.md) and [docs/FAUST_PLUGINS.md](docs/FAUST_PLUGINS.md) build one step by step: a C++ plugin, and a Faust program as a compiled module or as an `fstr` program.

- The `csnd`, `chuk` and `fstr` READMEs walk through one example each, from the card to the jacks, encoders and status panel. `fstr`'s builds `examples/filter_cv.dsp`, `filter.dsp` with cutoff CV on input 3, which now ships with the other examples.

- Every port's README links its engine's folder in sk-engines, and mentions of [cyfaust](https://github.com/shakfu/cyfaust) and [daisy-apps](https://github.com/shakfu/daisy-apps) link the projects.
