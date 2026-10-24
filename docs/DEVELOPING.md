# Developing

How this repo builds SSP modules, and which route to take for a new one. Setup and build commands are in [BUILDING.md](BUILDING.md).

## Choosing a route

| Route | You write | Rebuild to change it | CV | Guide |
|-|-|-|-|-|
| C++ engine | an `Engine` and a `PluginProcessor` | yes | any input, mapped in C++ | [CPP_PLUGINS.md](CPP_PLUGINS.md) |
| compiled Faust | a `.dsp` and three short files | yes | one input per control, added for you | [FAUST_PLUGINS.md](FAUST_PLUGINS.md#tutorial-a-a-compiled-module) |
| `fstr` program | a `.dsp` on the card | no | audio inputs only | [FAUST_PLUGINS.md](FAUST_PLUGINS.md#tutorial-b-a-program-for-fstr) |
| `csnd` program | a `.csd` on the card | no | audio inputs only | [plugins/csound/README.md](../plugins/csound/README.md#walkthrough-filtercsd) |
| `chuk` program | a `.ck` on the card | no | audio inputs only | [plugins/chuck/README.md](../plugins/chuck/README.md#walkthrough-filterck) |

A program on the card is the fastest to try: no build, and Load swaps it in. A module of its own costs a build, but it gets its own name in Synthor, its own parameters in presets, and CV inputs. CPU is close: on the SSP, `chorus.dsp` measured 1.3% average as `chrs`, and 1.3% average, 3.3% peak in `fstr`.

## Layers

| Path | What | Used by |
|-|-|-|
| `ssp-sdk/` | Percussa's `Percussa.h`: the C entry points Synthor calls | every plugin |
| `plugins/common/` | TheTechnobear's framework: `BaseProcessor` (JUCE `AudioProcessor` plus SSP glue), views, editors, controls, the file browser; `SSPApi.h` requires a class named `PluginProcessor` | every plugin |
| `plugins/common/engine/` | the engine layer: `Engine`, `EngineProcessor`, `EngineEditor`, and the Faust and script hosts on top | all but `sfct`, `rack` and `ttyp` |
| `examples/` | `svca` and `tremolo`, the guides' worked examples; built and tested on the host, never shipped | the guides |

[dev/engines.md](dev/engines.md#layout) lists every file in the engine layer. Its three core types:

- `Engine` (`Engine.h`): `prepare`, `process` and `idle`. No JUCE, so an engine tests with a plain compiler.

- `EngineProcessor`: copies the inputs, splits blocks larger than prepared, calls `control()` and then `Engine::process` on the audio thread, runs `Engine::idle` on a worker thread every 10 ms, and measures DSP load.

- `EngineEditor` and `EngineMiniEditor`: the full-screen editor and rack's compact one. Both draw `ParamPage`s: four controls a page, one per encoder.

`FaustProcessor` builds the parameters, jacks and pages from a compiled Faust kernel. `ScriptProcessor` hosts a program loaded from the card, with controls `p1`..`p16`; `csnd`, `chuk`, `fstr`, `scsy`, `sapf` and `alda` use it.

## Tools

| Tool | What |
|-|-|
| `make deps` (`scripts/build_deps.sh ssp\|host`) | builds libsndfile, Csound, the ChucK core, libfaust and libscsynth as static archives, and SC's and sc3-plugins' UGens as `.so` files, into `build/deps/<target>`; then `build_fluidsynth.sh`, and the `fetch_*.sh` scripts for teletype, sapf, alda and _core |
| `make faust-kernels` (`scripts/faust_kernel.sh`) | compiles a `.dsp` to a kernel header with [cyfaust](https://github.com/shakfu/cyfaust), fetched by `uv`. Kernel headers are committed, so a build does not need it |
| `make scsy-builtin` (`scripts/scsy_builtin.py`) | regenerates `scsy`'s built-in SynthDef header with nanosynth, fetched by `uv`. The header is committed |
| `scripts/new_plugin.py DIR NAME [--faust]` | copies `examples/svca`, or `examples/tremolo`, to `plugins/DIR` as module NAME, renamed and registered in the build |
| `make` | cross build for the SSP into `build.cmake.ssp` |
| `make test` | every pytest suite: the tools, the scripts, and `plugins/*/tests` |
| `plugins/common/tests/plugin_host.cpp` | loads a plugin `.so` through the SSP API on the host: sets state, feeds inputs, runs blocks, prints levels and state, renders the editor to a 1600x480 BGRA file. The plugin tests drive it |
| `make install [MOD=name]` (`scripts/install.sh`) | copies built plugins, or the one named, to the mounted card's `BOOT/plugins` |
| `make deploy`, `make deploy-mod MOD=name` | copy all plugins, or one, to `SSP_HOST` over `scp`; `deploy-mod` refuses while Synthor runs |
| `make install-faust`, `make install-scsy`, `make install-sapf`, `make install-fsyn`, `make install-brks`, `make install-presets` | the Faust libraries and `fstr` examples; the UGen plugins and `scsy` examples; sapf's prelude and examples; `fsyn`'s soundfont; `brks`'s default banks; rack presets |
| `make midisend` (`tools/midisend`) | a MIDI input for tests, without a keyboard: builds it, copies it to `SSP_HOST`'s `/tmp` and starts it. Choose `midisend` as a module's MIDI input in its general panel (LS + RS), then play it over ssh: `ssh $SSP_HOST 'echo "on 60 100" > /tmp/midisend'`, and `off 60`. A reboot removes it |
| `make mididump` (`tools/mididump`) | prints the notes and controllers of the SSP's MIDI ports whose names contain a word: builds it and copies it to `SSP_HOST`'s `/tmp`. `ssh $SSP_HOST /tmp/mididump gseq` shows what `gseq` sends; `LPMiniMK3` what the Launchpad sends. A reboot removes it |
| `scripts/ssp_card_image.py` | image a whole SD card, or write an image to one: see [Card images](#card-images) |
| `plugins/scsynth/tests/run_on_ssp.sh` | cross-compiles the `scsy` tests and runs them on the SSP over ssh |
| `tools/py2rack` | writes, checks and decodes rack presets off the device. See its [README](../tools/py2rack/README.md) |
| `make release`, `make publish` | the release package and its GitHub release. See [BUILDING.md](BUILDING.md) |


### Card images

`scripts/ssp_card_image.py` copies a whole SSP card to a compressed file, and writes such a file to another card. It runs as root. Check the device name with `lsblk` first: `write` overwrites the whole device.

```
sudo python3 scripts/ssp_card_image.py create /dev/sdX card.img.zst
sudo python3 scripts/ssp_card_image.py write card.img.zst /dev/sdX
```

- `create` zeroes the free space of BOOT and rootfs first, so it compresses away; `--no-zero` skips this. rootfs uses `zerofree` when installed.
- The image runs from sector 0 to the end of the last partition, so it holds the boot loader.
- The suffix picks the compressor: `.zst`, `.xz` or `.gz`. A sidecar `card.img.zst.json` holds the size and SHA-256.
- `write` refuses a smaller card, checks the written card against the SHA-256, and moves the backup GPT to the card's end.
- `--yes` skips the questions.
## Naming

Synthor lists a plugin only if its uid spells its name. So `PRODUCT_NAME` has at most four characters, and `PLUGIN_CODE` is the same name in capitals: `trem`/`TREM`. [dev/engines.md](dev/engines.md#naming) has the evidence.

## Threads

- Audio: `control()` then `Engine::process`. Never block or allocate.

- Worker: `Engine::idle`, every 10 ms. File reads and compiles go here.

- Message: `prepare`, state save and restore, the editor.

[dev/engines.md](dev/engines.md#threads) shows how the file-streaming plugins hand data between them.
