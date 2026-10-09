# ssp

Plugins and tools for the Percussa SSP.

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
| `plugins/common` | shared SSP plugin framework, from TheTechnobear; `engine/` is shared by the engine plugins. See [docs/dev/engines.md](docs/dev/engines.md) |
| `presets` | rack presets, installed with `make install-presets`. See [README](presets/README.md) |
| `tools/py2rack` | generate and decode rack presets offline. See [README](tools/py2rack/README.md) |
| `external/softcut-lib` | vendored, patched copy of monome's softcut |

## Build

```
git submodule update --init --recursive
make deps     # build Csound, ChucK and libfaust for the csound, chuck and faust plugins
make          # cross build for the SSP
make test     # all tests
```

See [docs/BUILDING.md](docs/BUILDING.md).

## Origin

Forked from [TheTechnobear/SSP](https://github.com/TheTechnobear/SSP) (Mark Harris). `plugins/common` and the trax code in `rack` are his work; his other plugins are removed. Licence: AGPL-3.0, see [LICENSE](LICENSE). softcut and `plugins/glitch` are GPL-3.0.
