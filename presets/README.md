# presets

rack presets. `make install-presets` checks the JSON ones with `py2rack` and copies the directory to `rack_presets` on the SD card's BOOT partition. `make test` checks them too, and parses their matrices with rack's own loader. Format: [plugins/rack/README-json-presets.md](../plugins/rack/README-json-presets.md).

| preset | what it shows |
|-|-|
| `empty.json` | Four empty tracks: a starting point for patching on the device. Load modules on the track page, wire them in the routing view, then save under a new name. |
| `matrix_example.json` | Routing as matrices. Track 1: `omod` triggers three `drum` voices, panned by gain. Track 2: `clds` into `srvb`, with `srvb` fed back into `clds` (a wire that reads the previous block, orange in the routing view), and a `dc` row offsetting CV from input 3 into `clds` `Pos`. Needs `omod`, `drum`, `clds` and `srvb` installed. |
| `plaits_voice.json` | `omod` triggers `plts`; its slower outputs modulate `Timbre` and `Harm`, and a `dc` row sets `Harm`'s centre. `plts` `Out` goes through `srvb`, `Aux` straight to the output. |
| `rings_feedback.json` | `omod` strums `rngs`, exciting it with input 1. `rngs` goes through `dlyd`, and `dlyd` `Out 1` feeds back into `rngs` `In`: a loop, so that wire reads the previous block (orange). |
| `quantized_melody.json` | `omod` `Out A` is sampled and quantised by `shq` on each `Main` pulse. The pitch and trigger play `plts`, filtered by `ldrf`, with `omod` `Out B` sweeping the cutoff around a `dc` offset. |
| `input_fx.json` | Audio in on inputs 1-2: `comp` (sidechained from input 3), then `clds`, then `srvb`, with the dry compressed signal mixed in. Needs audio routed into rack in Synthor. |
| `euclid_drums.json` | * `omod` clocks `edrm` at sixteenths, 120 BPM. Kick 4 of 16, tom 3 of 12 (polymeter), snare 2 of 16 rotated, hat 7 of 16 with swing. The snare goes to `srvb`. |
| `bass_line.json` | `omod` `Out A` (eighths) triggers `plts` and samples a slow saw through `shq` into its pitch, two octaves down. `ldrf` filters it, its cutoff swept by `Out C`; then `comp`. |
| `ambient_rings.json` | A slow `omod` strums `rngs` at pitches `shq` quantises from a saw; `Out B` moves the strike position. `clds`, then `srvb`. |
| `glitch_chorus.json` | * `gltc`: deck A `tri xor`, deck B `dust`, split left and right, their parameters swept by slow LFOs. Then `chrs`, then `srvb`. |
| `radio_drift.json` | * `rdio`: deck A drifts across the stations of its bank on a slow triangle; deck B resets on pulses and moves its start. `clds`, then `srvb`. Needs a Radio Music library in `BOOT/radio`. |
| `dub_delay.json` | Inputs 1-2 into `dlyd`, whose output `ldrf` filters and feeds back into it (orange wires). A slow LFO moves the cutoffs. Dry, delay and `srvb` mixed. Needs audio into rack. |
| `harmonic_drone.json` | `harm`, an additive oscillator, with its spectral centre, spread and amount swept by three LFOs at unrelated rates. `clds`, then `srvb`. |
| `jam_four_tracks.json` | * Four tracks. 1: `edrm` and a `plts` bass on one `omod` clock. 2: `rngs` strums into `srvb`. 3: `rdio` deck A into `clds`. 4: inputs 1-2 through `comp` and `srvb`. |

\* uses this repo's modules (`edrm`, `gltc`, `chrs`, `rdio`).

Parameters are set only where a working value is known, mostly `omod` as a clock: with `Lfo` on, `Freq` is in hundredths of a hertz, so 800 is 8 Hz, sixteenths at 120 BPM. The rest use module defaults; tune them on the device and save over the preset.

Tracks share no wires, so each track that needs a clock has its own `omod`; `jam_four_tracks.json` keeps the drums and bass on one track for that reason.
