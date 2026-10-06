# ssp

Plugins and tools for the Percussa SSP.

| Path | What |
|-|-|
| `plugins/sfct` | softcut 4-track looper. See [README](plugins/sfct/README.md) |
| `plugins/rack` | preset-first module host; fork of TheTechnobear's trax with JSON presets. See [README](plugins/rack/README.md) |
| `plugins/common` | shared SSP plugin framework, from TheTechnobear |
| `presets` | rack presets, installed with `make install-presets`. See [README](presets/README.md) |
| `tools/py2rack` | generate and decode rack presets offline. See [README](tools/py2rack/README.md) |
| `external/softcut-lib` | vendored, patched copy of monome's softcut |

## Build

```
git submodule update --init --recursive
make          # cross build for the SSP
make test     # py2rack and sfct engine tests
```

See [docs/BUILDING.md](docs/BUILDING.md).

## Origin

Forked from [TheTechnobear/SSP](https://github.com/TheTechnobear/SSP) (Mark Harris).
`plugins/common` and the trax code in `rack` are his work; his other plugins are removed.
Licence: AGPL-3.0, see [LICENSE](LICENSE). softcut is GPL-3.0.
