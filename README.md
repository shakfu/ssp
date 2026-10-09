# ssp

Plugins and tools for the Percussa SSP.

Most of the modules are ports of engines from [sk-engines](https://github.com/shakfu/sk-engines), a platform fork of the Synthux Spotykach firmware that swaps DSP engines on the same hardware. `rdio`, `csnd`, `chuk`, `edrm`, `strc`, `bard` and `gltc` are its engines, and `chrs` takes its Faust DSP from it. [docs/dev/engines.md](docs/dev/engines.md) lists what changed in the port.

| Path | What |
|-|-|
| `plugins/sfct` | softcut 4-track looper. See [README](plugins/sfct/README.md) |
| `plugins/rack` | preset-first module host; fork of TheTechnobear's trax with JSON presets. See [README](plugins/rack/README.md) |
| `plugins/radio` | `rdio`: dual virtual Radio Music. See [README](plugins/radio/README.md) |
| `plugins/csound` | `csnd`: Csound 7 host. See [README](plugins/csound/README.md) |
| `plugins/chuck` | `chuk`: ChucK host. See [README](plugins/chuck/README.md) |
| `plugins/edrums` | `edrm`: four-track Euclidean drum machine. See [README](plugins/edrums/README.md) |
| `plugins/pstretch` | `strc`: dual real-time PaulStretch. See [README](plugins/pstretch/README.md) |
| `plugins/bard` | `bard`: dual spoken-word player with bookmarks. See [README](plugins/bard/README.md) |
| `plugins/glitch` | `gltc`: dual lo-fi noise voice, GPLv3. See [README](plugins/glitch/README.md) |
| `plugins/chorus` | `chrs`: stereo chorus compiled from Faust. See [README](plugins/chorus/README.md) and [docs/dev/faust.md](docs/dev/faust.md) |
| `plugins/faust` | `fstr`: Faust programs from the card, compiled by LLVM on the SSP. See [README](plugins/faust/README.md) |
| `plugins/common` | shared SSP plugin framework, from TheTechnobear; `engine/` is shared by the engine plugins. See [docs/DEVELOPING.md](docs/DEVELOPING.md) |
| `examples` | `svca` and `tremolo`, the worked examples of the plugin guides; built and tested on the host, never shipped |
| `presets` | rack presets, installed with `make install-presets`. See [README](presets/README.md) |
| `tools/py2rack` | generate and decode rack presets offline. See [README](tools/py2rack/README.md) |
| `external/softcut-lib` | vendored, patched copy of monome's softcut |

## Build

```
git submodule update --init --recursive
make deps     # build Csound, ChucK and libfaust for the csound, chuck and faust plugins
make          # cross build for the SSP
make test     # all tests
make release  # package releases/shakfu-ssp-plugins-<VERSION> (see VERSION) and its zip
```

See [docs/BUILDING.md](docs/BUILDING.md).

## Writing plugins

- [docs/DEVELOPING.md](docs/DEVELOPING.md): the shared code and tools, and which route to take.

- [docs/CPP_PLUGINS.md](docs/CPP_PLUGINS.md): a C++ plugin on the engine layer.

- [docs/FAUST_PLUGINS.md](docs/FAUST_PLUGINS.md): a Faust program, compiled into a module or loaded by `fstr`.

- The `csnd`, `chuk` and `fstr` READMEs each walk through one example program on the SSP.

## Origins

- [sk-engines](https://github.com/shakfu/sk-engines): the engines behind `rdio`, `csnd`, `chuk`, `edrm`, `strc`, `bard` and `gltc`, and the DSP of `chrs`.

- [TheTechnobear/SSP](https://github.com/TheTechnobear/SSP) (Mark Harris): this repo is a fork. `plugins/common` and the trax code in `rack` are his work; his other plugins are removed.

- [monome softcut](https://github.com/monome/softcut-lib): the engine of `sfct`, vendored in `external/softcut-lib`.

- Rob Scape's [Noisferatu](https://github.com/rob-scape/noisferatu): the algorithms of `gltc`.

Licence: AGPL-3.0, see [LICENSE](LICENSE). softcut and `plugins/glitch` are GPL-3.0.
