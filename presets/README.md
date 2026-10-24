# presets

Example presets for [rack](../plugins/rack/README.md). Format: [README-json-presets.md](../plugins/rack/README-json-presets.md).

`make install-presets` checks the JSON presets with [py2rack](../tools/py2rack/README.md), then copies them to `BOOT/rack_presets`: to a mounted card, or over ssh to the SSP ([card paths](../docs/USING.md#card-paths)). Each preset needs the modules in its Needs column installed in `BOOT/plugins`.

| preset | Needs | what it shows |
|-|-|-|
| `empty.json` | | Four empty tracks, to patch on the device. Load modules in the slot view, wire them in the routing view, then save under a new name. |
| `matrix_example.json` | `omod` `drum` `clds` `srvb` | Routing as matrices. Track 1: `omod` triggers three `drum` voices, panned by gain. Track 2: `clds` into `srvb`, with `srvb` fed back into `clds` (a wire that reads the previous block, orange in the routing view), and a `dc` row offsetting CV from input 3 into `clds` `Pos`. |
| `plaits_voice.json` | `omod` `plts` `srvb` | `omod` triggers `plts`; its slower outputs modulate `Timbre` and `Harm`, and a `dc` row sets `Harm`'s centre. `plts` `Out` goes through `srvb`, `Aux` straight to the output. |
| `rings_feedback.json` | `omod` `rngs` `dlyd` | `omod` strums `rngs`, exciting it with input 1. `rngs` goes through `dlyd`, and `dlyd` `Out 1` feeds back into `rngs` `In`. That wire closes a loop, so it reads the previous block (orange). |
| `quantized_melody.json` | `omod` `shq` `plts` `ldrf` | `omod` `Out A` is sampled and quantised by `shq` on each `Main` pulse. The pitch and trigger play `plts`, filtered by `ldrf`, with `omod` `Out B` sweeping the cutoff around a `dc` offset. |
| `input_fx.json` | `comp` `clds` `srvb` | Audio in on inputs 1-2: `comp` (sidechained from input 3), then `clds`, then `srvb`, with the dry compressed signal mixed in. Needs audio routed into rack in Synthor. |
| `euclid_drums.json` | `omod` `edrm` `srvb` | `omod` clocks `edrm` at sixteenths, 120 BPM. Kick 4 of 16, tom 3 of 12 (polymeter), snare 2 of 16 rotated, hat 7 of 16 with swing. The snare goes to `srvb`. |
| `bass_line.json` | `omod` `shq` `plts` `ldrf` `comp` | `omod` `Out A` (eighths) triggers `plts` and samples a slow saw through `shq` into its pitch, two octaves down. `ldrf` filters it, its cutoff swept by `Out C`; then `comp`. |
| `ambient_rings.json` | `omod` `shq` `rngs` `clds` `srvb` | A slow `omod` strums `rngs` at pitches `shq` quantises from a saw; `Out B` moves the strike position. `clds`, then `srvb`. |
| `glitch_chorus.json` | `omod` `gltc` `chrs` `srvb` | `gltc`: deck A `tri xor`, deck B `dust`, split left and right, their parameters swept by slow LFOs. Then `chrs`, then `srvb`. |
| `radio_drift.json` | `omod` `rdio` `clds` `srvb` | `rdio`: deck A drifts across the stations of its bank on a slow triangle; deck B resets on pulses and moves its start. `clds`, then `srvb`. Needs a Radio Music library in `BOOT/radio`. |
| `dub_delay.json` | `dlyd` `ldrf` `srvb` `omod` | Inputs 1-2 into `dlyd`, whose output `ldrf` filters and feeds back into it (orange wires). A slow LFO moves the cutoffs. Dry, delay and `srvb` mixed. Needs audio into rack. |
| `harmonic_drone.json` | `omod` `harm` `clds` `srvb` | `harm`, an additive oscillator, with its spectral centre, spread and amount swept by three LFOs at unrelated rates. `clds`, then `srvb`. |
| `jam_four_tracks.json` | `omod` `edrm` `shq` `plts` `ldrf` `rngs` `srvb` `rdio` `clds` `comp` | Four tracks. 1: `edrm` and a `plts` bass on one `omod` clock. 2: `rngs` strums into `srvb`. 3: `rdio` deck A into `clds`. 4: inputs 1-2 through `comp` and `srvb`. |

`edrm`, `gltc`, `chrs` and `rdio` are this repo's modules. The others are TheTechnobear's.

## omod as a clock

Parameters are set only where a working value is known, mostly `omod` as a clock. With `Lfo` on, `Freq` is in hundredths of a hertz: 800 is 8 Hz, sixteenths at 120 BPM. The rest use module defaults; tune them on the device and save over the preset.

Tracks share no wires, so each track that needs a clock has its own `omod`. `jam_four_tracks.json` keeps the drums and bass on one track for that reason.
