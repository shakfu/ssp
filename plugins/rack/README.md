# rack

rack hosts SSP modules in 4 tracks of 8 slots. It is a fork of TheTechnobear's trax 1.0.1, built around presets authored on a desktop. Preset format: [README-json-presets.md](README-json-presets.md). Design notes: [docs/dev/rack-design.md](../../docs/dev/rack-design.md).

## Routing model

A track's routing is a gain matrix. Rows are source jacks, columns are destination jacks. Each destination input receives:

```
input[j] = sum over sources i of (gain[i][j] * source[i]) + dc[j]
```

- **A connection is a nonzero gain.** Gain 0 means no connection.
- **The preset records the signal, not the editing state.** A wire set to gain 0 and a wire never made both save as 0, and both reload as no wire. rack does not store muted connections.
- **`dc` is a constant added to an input,** one value per destination jack. It needs at least one wire into that jack.
- **Gain may be negative** in a preset, which inverts the source. Gain is not normalised and has no upper limit. 1 passes the source unchanged.
- **Gain applies to every wire,** including wires from the track input (`in`) and to the track output (`out`). The track level on the mixer page applies after `out`.

Internally the engine keeps a list of wires, each with its own gain and offset. That is an implementation detail. A save collapses it to the matrix above: repeated wires between the same jacks sum into one cell, and the offsets into a jack sum into its `dc`.

## Routing view

On the track page, short Down opens the routing view; Up returns.

- **Grid:** one cell per module pair. Rows are sources (`from`), columns destinations (`to`), headed by module name. A cell shows how many wires connect the pair. Green wires run in order; orange wires read the previous block (a feedback loop); red wires connect a module to itself and carry nothing.
- **Detail pane:** each of the first four lines starts with the number of the encoder that changes it. Below them, the selected cell's wires, as `Main -> HH1 Trig`.

| Control | `Level` off | `Level` on |
|-|-|-|
| Encoder 1 | source module | source module |
| Encoder 2 | destination module | destination module |
| Encoder 3 | source jack | offset of the selected wire, saved into the destination's `dc` |
| Encoder 4 | destination jack | gain, 0 to 1 |
| Push encoder 4 | connect at gain 1, or disconnect | same |

Turning gain up from 0 connects; turning it down to 0 disconnects. The device keeps gain between 0 and 1 in steps of 0.01. A preset can set any gain; a gain above 1 is clamped to 1 the first time it is edited on the device.
