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

- **Grid:** one cell per module pair. Rows are sources (`from`), columns destinations (`to`), headed by module name. Empty slots have no jacks and are left out, so load modules before wiring them. A cell shows how many wires connect the pair. Green wires run in order; orange wires read the previous block (a feedback loop); red wires connect a module to itself and carry nothing.
- **Jack matrix,** beside the grid: the selected module pair's jacks. Rows are the source's outputs, columns the destination's inputs. A filled cell is a wire; its brightness is its gain, also printed in the cell when it fits. The yellow frame is the jack cursor.
- **Wire list:** on the SSP's full 1600 px screen, a third column lists every wire on the track, as `omod Main -> drum HH1 Trig  x1.00`. The selected pair's wires are white, the cursor's wire yellow.
- **Badges:** a grey number names the encoder that moves each cursor. The status line under the jack matrix shows the cursor's jacks and its gain and offset, as `Main -> HH1 Trig`, `x1.00 +0.00`.

| Control | `Level` off | `Level` on |
|-|-|-|
| Encoder 1 | source module | source module |
| Encoder 2 | destination module | destination module |
| Encoder 3 | source jack | offset of the selected wire, -1 to 1 (+-5 V), saved into the destination's `dc` |
| Encoder 4 | destination jack | gain, 0 to 1 |
| Press encoder 4 | connect at gain 1, or disconnect | same |

Soft key 1 switches `Level`; full screen, the title line shows its state. Offset and gain move 0.1 a detent, or 0.01 while the encoder is held. Encoder 4's press acts on release, and not after the encoder was turned while held.

Turning gain up from 0 connects; turning it down to 0 disconnects. The device keeps gain between 0 and 1 and offset between -1 and 1.

A module's input is the sum of its wires, so a feedback loop or stacked wires can push it anywhere. Before each module runs, rack sets non-finite input samples to 0 and limits the rest to +-2 (+-10 V at 0.2 a volt), so a runaway loop saturates instead of handing the module infinity or NaN. A preset can set any gain; a gain above 1 is clamped to 1 the first time it is edited on the device.
