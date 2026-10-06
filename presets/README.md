# presets

rack presets. `make install-presets` checks the JSON ones with `py2rack` and copies the directory to `/rack_presets` on the SD card. `make test` checks them too. Format: [plugins/rack/README-json-presets.md](../plugins/rack/README-json-presets.md).

| preset | what it shows |
|-|-|
| `matrix_example.json` | Routing as matrices. Track 1: `omod` triggers three `drum` voices, panned by gain. Track 2: `clds` into `srvb`, with `srvb` fed back into `clds` (a wire that reads the previous block, orange in the routing view), and a `dc` row offsetting CV from input 3 into `clds` `Pos`. Needs `omod`, `drum`, `clds` and `srvb` installed. |
| `plaits_voice.json` | `omod` triggers `plts`; its slower outputs modulate `Timbre` and `Harm`, and a `dc` row sets `Harm`'s centre. `plts` `Out` goes through `srvb`, `Aux` straight to the output. |
| `rings_feedback.json` | `omod` strums `rngs`, exciting it with input 1. `rngs` goes through `dlyd`, and `dlyd` `Out 1` feeds back into `rngs` `In`: a loop, so that wire reads the previous block (orange). |
| `quantized_melody.json` | `omod` `Out A` is sampled and quantised by `shq` on each `Main` pulse. The pitch and trigger play `plts`, filtered by `ldrf`, with `omod` `Out B` sweeping the cutoff around a `dc` offset. |
| `input_fx.json` | Audio in on inputs 1-2: `comp` (sidechained from input 3), then `clds`, then `srvb`, with the dry compressed signal mixed in. Needs audio routed into rack in Synthor. |

Parameters are set only where a working value is known, mostly `omod` as a clock. The rest use module defaults; tune them on the device and save over the preset.
