# ssp

19 modules for the [Percussa SSP](https://www.percussa.com/), a Eurorack module whose host software, Synthor, loads them. Also the tools to build, test and write more.

## Install

1. Download `shakfu-ssp-plugins-<VERSION>.zip` from the [releases](https://github.com/shakfu/ssp/releases).
2. Copy the contents of its `BOOT/` folder onto the SD card's BOOT partition.
3. Restart the SSP. Synthor lists each module by its 4-letter name.

[docs/USING.md](docs/USING.md) covers what every module shares: patching, buttons, the general panel and USB devices. Each module's README covers the rest.

## Modules

Loopers and players:

| Module | What | Docs |
|-|-|-|
| `sfct` | softcut looper: 8 voices on 4 tracks | [README](plugins/sfct/README.md) |
| `brks` | the breakbeat slicer firmware of _core; plays sample-manager card folders | [README](plugins/breaks/README.md) |
| `rdio` | dual virtual Radio Music | [README](plugins/radio/README.md) |
| `strc` | dual real-time PaulStretch | [README](plugins/pstretch/README.md) |
| `bard` | dual spoken-word player with bookmarks | [README](plugins/bard/README.md) |

Synths, drums and effects:

| Module | What | Docs |
|-|-|-|
| `dexd` | 6-operator FM synth played from MIDI, on the Dexed engine; DX7 cartridges from the card | [README](plugins/dexed/README.md) |
| `fsyn` | SF2 soundfont synth played from MIDI, on FluidSynth; ships the FluidR3_GM General MIDI set | [README](plugins/fsyn/README.md) |
| `gltc` | dual lo-fi noise voice | [README](plugins/glitch/README.md) |
| `edrm` | four-track Euclidean drum machine | [README](plugins/edrums/README.md) |
| `chrs` | stereo chorus compiled from Faust | [README](plugins/chorus/README.md) |

Sequencers:

| Module | What | Docs |
|-|-|-|
| `gseq` | grid sequencer and piano roll, out as MIDI and CV; optional Launchpad Mini MK3 or X control | [README](plugins/gseq/README.md) |
| `alda` | an Alda score as gates, pitch CV and MIDI, in a loop | [README](plugins/alda/README.md) |

Live coding, from the card or a USB keyboard ([script editor](docs/script-editor.md)):

| Module | What | Docs |
|-|-|-|
| `csnd` | Csound 7 orchestras | [README](plugins/csound/README.md) |
| `chuk` | ChucK programs | [README](plugins/chuck/README.md) |
| `fstr` | Faust programs, compiled by LLVM on the SSP | [README](plugins/faust/README.md) |
| `scsy` | SuperCollider SynthDefs, run in scsynth | [README](plugins/scsynth/README.md) |
| `sapf` | the sapf live-coding language | [README](plugins/sapf/README.md) |
| `ttyp` | monome teletype | [README](plugins/teletype/README.md), [getting started](plugins/teletype/README-getting-started.md) |

Host:

| Module | What | Docs |
|-|-|-|
| `rack` | runs up to 8 modules on each of 4 tracks in one slot; fork of TheTechnobear's trax, with JSON presets | [README](plugins/rack/README.md), [presets](presets/README.md) |

## Build from source

```text
git submodule update --init   # not --recursive: ssp-sdk nests a JUCE clone the build does not use
make deps     # once: the libraries and sources nine modules need
make          # cross build for the SSP
make test     # all tests
make release  # package releases/shakfu-ssp-plugins-<VERSION> and its zip
```

[docs/BUILDING.md](docs/BUILDING.md) has the setup, every target, and ssh access to the SSP.

## Writing plugins

- [docs/DEVELOPING.md](docs/DEVELOPING.md): the shared code and tools, and which route to take.
- [docs/CPP_PLUGINS.md](docs/CPP_PLUGINS.md): a C++ plugin on the engine layer.
- [docs/FAUST_PLUGINS.md](docs/FAUST_PLUGINS.md): a Faust program, compiled into a module or loaded by `fstr`.
- The `csnd`, `chuk` and `fstr` READMEs each walk through one example program on the SSP.

## Repository layout

| Path | What |
|-|-|
| `plugins/<dir>` | one folder per module; the table above links each README |
| `plugins/common` | the shared SSP plugin framework, from TheTechnobear; `engine/` is shared by the engine plugins. See [docs/DEVELOPING.md](docs/DEVELOPING.md) |
| `examples` | `svca` and `tremolo`, the worked examples of the plugin guides; built and tested on the host, never shipped |
| `presets` | rack presets, installed with `make install-presets`. See [README](presets/README.md) |
| `tools/py2rack` | generate and decode rack presets offline. See [README](tools/py2rack/README.md) |
| `external/softcut-lib` | vendored, patched copy of monome's softcut |
| `docs/dev` | design records, device tests and investigation notes |

## Origins

- [sk-engines](https://github.com/shakfu/sk-engines), a platform fork of the Synthux Spotykach firmware that swaps DSP engines on the same hardware: the engines behind `rdio`, `csnd`, `chuk`, `edrm`, `strc`, `bard` and `gltc`, and the DSP of `chrs`.

- [TheTechnobear/SSP](https://github.com/TheTechnobear/SSP) (Mark Harris): this repo is a fork. `plugins/common` and the trax code in `rack` are his work; his other plugins are removed.

- [monome softcut](https://github.com/monome/softcut-lib): the engine of `sfct`, vendored in `external/softcut-lib`.

- Rob Scape's [Noisferatu](https://github.com/rob-scape/noisferatu): the algorithms of `gltc`.

- [monome teletype](https://github.com/monome/teletype), run on the mock hardware of Dewb's [monome-rack](https://github.com/Dewb/monome-rack): the firmware of `ttyp`.

- Zack Scholl's [_core](https://github.com/schollz/_core): the firmware of `brks` and its desktop engine.

- [Synth_Dexed](https://codeberg.org/dcoredump/Synth_Dexed), the [Dexed](https://github.com/asb2m10/dexed) engine: the synth of `dexd`.

- [sapf](https://github.com/lfnoise/sapf), through the fork [shakfu/sapf](https://github.com/shakfu/sapf): the language of `sapf`.

- [psnd](https://github.com/shakfu/psnd)'s C implementation of [Alda](https://alda.io): the core of `alda`.

- [FluidSynth](https://github.com/FluidSynth/fluidsynth): the synth of `fsyn`.

- [SuperCollider](https://github.com/supercollider/supercollider)'s scsynth and UGens, with [sc3-plugins](https://github.com/supercollider/sc3-plugins): the server of `scsy`.

## Licence

The repository is AGPL-3.0; see [LICENSE](LICENSE). These modules carry their own licence, for the code they include:

- GPL-3.0: `sfct` (softcut), `gltc`, `ttyp`, `dexd`, `sapf`, `alda`, `scsy`.
- GPL-3.0-only: `brks`.
- AGPL-3.0 with FluidSynth (LGPL-2.1-or-later): `fsyn`; its `FluidR3_GM.sf2` is MIT.
