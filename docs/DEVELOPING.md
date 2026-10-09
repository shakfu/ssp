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
| `ssp-sdk/` | Percussa's `SSPApi.h`: the C entry points Synthor calls | every plugin |
| `plugins/common/` | TheTechnobear's framework: `BaseProcessor` (JUCE `AudioProcessor` plus SSP glue), views, editors, controls, the file browser; `SSPApi.h` exports a `PluginProcessor` | every plugin |
| `plugins/common/engine/` | the engine layer: `Engine`, `EngineProcessor`, `EngineEditor`, and the Faust and script hosts on top | all but `sfct` and `rack` |
| `examples/` | `svca` and `tremolo`, the guides' worked examples; built and tested on the host, never shipped | the guides |

[dev/engines.md](dev/engines.md#layout) lists every file in the engine layer. Its three core types:

- `Engine` (`Engine.h`): `prepare`, `process` and `idle`. No JUCE, so an engine tests with a plain compiler.
- `EngineProcessor`: copies the inputs, splits blocks larger than prepared, calls `control()` and then `Engine::process` on the audio thread, runs `Engine::idle` on a worker thread every 10 ms, and measures DSP load.
- `EngineEditor` and `EngineMiniEditor`: the full-screen editor and rack's compact one. Both draw `ParamPage`s: four controls a page, one per encoder.

`FaustProcessor` builds the parameters, jacks and pages from a compiled Faust kernel. `ScriptProcessor` hosts a program loaded from the card, with controls `p1`..`p16`; `csnd`, `chuk` and `fstr` use it.

## Tools

| Tool | What |
|-|-|
| `make deps` (`scripts/build_deps.sh ssp\|host`) | builds libsndfile, Csound, the ChucK core and libfaust as static archives into `build/deps/<target>` |
| `make faust-kernels` (`scripts/faust_kernel.sh`) | compiles a `.dsp` to a kernel header with [cyfaust](https://github.com/shakfu/cyfaust), fetched by `uv`. Kernel headers are committed, so a build does not need it |
| `scripts/new_plugin.py DIR NAME [--faust]` | copies `examples/svca`, or `examples/tremolo`, to `plugins/DIR` as module NAME, renamed and registered in the build |
| `make` | cross build for the SSP into `build.cmake.ssp` |
| `make test` | every pytest suite: the tools, the scripts, and `plugins/*/tests` |
| `plugins/common/tests/plugin_host.cpp` | loads a plugin `.so` through the SSP API on the host: sets state, feeds inputs, runs blocks, prints levels and state, renders the editor to a 1600x480 BGRA file. The plugin tests drive it |
| `make install [MOD=name]` (`scripts/install.sh`) | copies built plugins, or the one named, to the mounted card's `BOOT/plugins` |
| `make install-faust`, `make install-presets` | the Faust libraries and `fstr` examples; rack presets |
| `tools/py2rack` | writes, checks and decodes rack presets off the device. See its [README](../tools/py2rack/README.md) |
| `make release`, `make publish` | the release package and its GitHub release. See [BUILDING.md](BUILDING.md) |

## Naming

Synthor lists a plugin only if its uid spells its name. So `PRODUCT_NAME` has at most four characters, and `PLUGIN_CODE` is the same name in capitals: `trem`/`TREM`. [dev/engines.md](dev/engines.md#naming) has the evidence.

## Threads

- Audio: `control()` then `Engine::process`. Never block or allocate.
- Worker: `Engine::idle`, every 10 ms. File reads and compiles go here.
- Message: `prepare`, state save and restore, the editor.

[dev/engines.md](dev/engines.md#threads) shows how the file-streaming plugins hand data between them.
