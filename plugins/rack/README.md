# rack

rack runs up to 8 SSP modules on each of 4 tracks inside one Synthor slot. It is a fork of TheTechnobear's trax 1.0.1, built around presets authored on a desktop. Preset format: [README-json-presets.md](README-json-presets.md).

## Install

Copy `rack.so` to `BOOT/plugins` (from source: `make install MOD=rack`) and restart the SSP. See [Using](../../docs/USING.md#installing).

- Presets go in `BOOT/rack_presets`. `make install-presets` checks this repo's [presets](../../presets/README.md) and copies them there.
- rack loads each module a preset names from `BOOT/plugins`, so install those modules too.

## Screens

| Screen | Opens from | What it does |
|-|-|-|
| mixer | start; hold Up from any screen | track levels. Soft keys 1-4 open tracks 1-4 |
| options | hold Up on the mixer | `Load Pr` and `Save Pr` |
| performance | Down on the mixer; hold Down from any screen | the parameters a preset lists under `performance` |
| track page | soft key 1-4 on the mixer | one soft key `Mod n` per slot |
| module editor | press `Mod n` | the slot's module, with its own controls |
| slot view | hold `Mod n` | load a module into slot n; wire its jacks one at a time |
| routing view | Down on the track page | every wire on the track; see [Routing view](#routing-view) |

Up returns from the slot view or routing view to the track page.

A preset name ending in `.json` saves as JSON; any other name saves in trax's binary format.

### Slot view

| Control | Does |
|-|-|
| Soft key 1 (`Load`) | choose a module: encoder 1 picks a category, encoder 2 a module; press encoder 2 or `Load` to load it |
| Soft key 4 (`Flip IO`) | switch between the slot's outputs and inputs |
| Encoder 1 | the other module |
| Encoder 2 | the slot's jack |
| Encoder 3 | the other module's jack. Press to connect or disconnect |
| Encoder 4 | gain; turned while pressed, offset |

In the module chooser, soft key 5 (`Clear`) empties the slot.

## First use

1. Load `empty.json`, or an example preset, from the options page (hold Up on the mixer).
2. Press soft key 1 on the mixer to open track 1.
3. Hold `Mod 1` to open the slot view. Press `Load`, choose a module, and press `Load` again.
4. Press Up for the track page, then Down for the routing view. Wire `in` to the module and the module to `out`.
5. Patch a jack of rack in Synthor; Synthor runs a module only while a jack is patched ([Using](../../docs/USING.md#a-module-runs-only-while-a-jack-is-patched)).
6. Hold Up for the mixer, hold Up again for the options page, and save under a new name.

The MIDI settings are in the general panel (LS + RS, see [Using](../../docs/USING.md#the-general-panel)). In a module's compact view, Up + Down opens it.

## Routing model

A track's routing is a gain matrix. Rows are source jacks, columns are destination jacks. Each destination input receives:

```text
input[j] = sum over sources i of (gain[i][j] * source[i]) + dc[j]
```

- **A connection is a nonzero gain.** Gain 0 means no connection.

- **A preset stores no muted wires.** A wire at gain 0 saves as 0 and reloads as no wire.

- **`dc` is a constant added to an input,** one value per destination jack. It needs at least one wire into that jack.

- **Gain may be negative** in a preset, which inverts the source. Gain is not normalised and has no upper limit. 1 passes the source unchanged.

- **Gain applies to every wire,** including wires from the track input (`in`) and to the track output (`out`). The track level on the mixer page applies after `out`.

- **Several wires between the same two jacks** save as one cell with their gains summed.

## Routing view

On the track page, short Down opens the routing view; Up returns.

- **Grid:** one cell per module pair.
  - Rows are sources (`from`), columns destinations (`to`), headed by module name.
  - Empty slots have no jacks and are left out. Load modules before wiring them.
  - A cell shows how many wires connect the pair, in one of 3 colours:

  | Colour | Wires |
  |-|-|
  | green | run in order |
  | orange | read the previous block (a feedback loop) |
  | red | connect a module to itself and carry nothing |

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

Soft key 1 switches `Level`; full screen, the title line shows its state. Offset and gain move 0.1 a detent, or 0.01 while the encoder is held. Encoder 4's press acts on release. A press during which the encoder turned does nothing.

Turning gain up from 0 connects; turning it down to 0 disconnects. The device keeps gain between 0 and 1 and offset between -1 and 1.

A module's input is the sum of its wires, so a feedback loop or stacked wires can push it out of range. Before each module runs, rack limits its input:

- Non-finite samples (infinity, NaN) become 0.
- The rest are clamped to +-2 (+-10 V at 0.2 a volt).
- A preset can set any gain. A gain above 1 is clamped to 1 the first time it is edited on the device.
