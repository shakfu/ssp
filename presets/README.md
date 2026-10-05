# presets

rack presets. `make install-presets` checks the JSON ones with `py2rack` and copies the directory to `/rack_presets` on the SD card. `make test` checks them too. Format: [plugins/rack/README-json-presets.md](../plugins/rack/README-json-presets.md).

| preset | what it shows |
|-|-|
| `matrix_example.json` | Routing as matrices. Track 1: `omod` triggers three `drum` voices, panned by gain. Track 2: `clds` into `srvb`, with `srvb` fed back into `clds` (a wire that reads the previous block, orange in the routing view), and a `dc` row offsetting CV from input 3 into `clds` `Pos`. Needs `omod`, `drum`, `clds` and `srvb` installed. |
