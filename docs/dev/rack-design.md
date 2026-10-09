# rack design options

Status: the player decision is open. Sections 2, 3 and 6 are implemented. This compares two directions for `plugins/rack`:

- **A. Player.** Presets are authored on a desktop. The device loads and plays them, with no on-device patching.

- **B. Keep the editor.** Add matrix authoring and graph-ordered execution. Keep the existing on-device UI.

These are not exclusive. Matrix authoring and graph ordering (sections 2 and 3) help both options. The real decision is whether to delete the on-device editing UI.

## 1. Design at the fork

- Each track has 10 nodes: `IN` (8 channels), slots 1-8, and `OUT` (2 channels). `Track::M_MAX` is 10.

- Routing is `Matrix::connections_`, a list of `(module, channel) -> (module, channel)` wires. Each wire has a `gain` and an `offset`.

- `Track::process` ran modules in slot-index order. A wire from a higher slot to a lower one read the previous block, a 128-sample delay (2.7 ms at 48 kHz). Section 3 replaces this.

- There are 4 tracks, each on its own thread. The threads are pinned to CPUs 2 and 3. No wire crosses tracks.

Source size, 4211 lines in total:

| Group | Files | Lines |
|-|-|-|
| Engine | `PluginProcessor`, `Track`, `Module`, `Matrix`, `JsonPreset`, `SSPApi` | 1904 |
| Editing UI | `TrackEditor`, `TrackView`, `ModuleView`, `LoadModuleView`, `MatrixView`, `PerformanceAdd`, `PerformanceEdit`, `ModuleComponent` | 1353 |
| Playing UI | `PluginEditor`, `MixerView`, `PerformanceEditor`, `PerformanceView`, `PerfParamComponent`, `OptionEditor`, `OptionView` | 954 |

`ModuleView` is listed as editing UI. It shows the hosted module's own editor, though, which is also useful while playing. See open question 2.

## 2. Matrix routing (both options, implemented)

A track's routing in a JSON preset is a gain matrix. rack loads it, rack's JSON save writes it, and `py2rack encode` accepts it. Format: `plugins/rack/README-json-presets.md`.

A jack-level matrix for one track is about 106 x 106, assuming 12 channels per slot, and a patch fills 10-30 cells. So the matrix is labelled and names only the jacks it uses:

```json
"matrix": {
  "rows": ["in:0", "in:1", "1:Out L", "dc"],
  "cols": ["1:In L", "1:In R", "out:0"],
  "gain": [[1, 0, 0  ],
           [0, 1, 0  ],
           [0, 0, 0.5],
           [0, 0, 0.1]]
}
```

- Every nonzero cell is one wire. The engine still stores wires; the matrix is the file format.

- `dc` is a constant source of 1.0, so each column computes `y = W x + b`. The engine adds `offset` once per wire, so on load a `dc` weight goes on the first wire into its column. On save, the offsets of all wires into a column add into its `dc` weight. A column with only a `dc` entry is an error, since there is no `dc` node to wire from.

- Saving is lossless in sound, not in structure: repeated wires between the same jacks merge into one cell with the summed gain, as the engine sums them anyway.

- Saves label jacks by channel index, which stays exact when a module repeats a channel name. Hand-written matrices can use names.

- `wires` still loads, and its wires are added to the matrix's. Older presets keep working.

One set of rules, two implementations: `jsonpreset::parseMatrix` and `formatMatrix` in C++, and `py2rack.matrix_wires` in Python. Tests: `plugins/rack/tests/host/json_matrix_test.cpp` (parsing, saving, and a load-save-reload through `Track`), and `test_matrix_*` in `tools/py2rack/tests/test_py2rack.py`.

An earlier version of this section kept the matrix in Python only, to leave the file format and loader unchanged. That made the matrix invisible on the device: presets on the card were wire lists, and device saves wrote wire lists. Matrix routing is the point of rack, so the matrix moved into the format.

## 3. Graph-ordered execution (both options, implemented)

`Track::process` runs modules in the order returned by `rack::executionOrder` (`Source/GraphOrder.h`). That is a topological sort of the track's module-level graph, not slot order.

- Ties go to the lowest slot, so a patch with no backward wires runs as before.

- When only cycles remain, the next module is the lowest slot whose remaining inputs all lie on its own cycles. Only those inputs read the previous block. A wire that is not on a cycle is never delayed.

- A cycle is broken at its lowest slot, so slot placement still chooses which wire of a cycle is delayed.

- Self-wires are ignored by the sort. They are dead in the engine anyway: `process` clears a module's buffer before summing its inputs, so a self-wire adds zeros.

The order is computed in `process` every block, not stored. It uses fixed arrays: 100 cells, plus a 1000-step transitive closure only when a cycle must be broken. That costs less than the wire scan `process` already does for each module. Nothing is cached, so the order cannot go stale or race with a routing change.

Effects:
- Slot position no longer changes the sound, except for which wire of a cycle is delayed.

- A preset with a backward wire not on a cycle loses that wire's 128-sample delay. It sounds different in rack than in upstream trax.

Tests: `tests/graph_order_test.cpp`, run by `make test`. It checks fixed cases and a property over 2000 random graphs: the order is a permutation, and every wire that runs backward lies on a cycle.

## 4. Option A: player

**Remove:** the editing UI, 1353 lines (1224 without `ModuleView`). **Keep:** the mixer (levels, mute), the performance page, and preset loading in `OptionEditor`.

Pros:
- About 30% less source to maintain, and none of the code removed is engine code.

- Data flows one way: script, then JSON file, then device. A device save never rewrites the file.

- The freed UI can be used for playing: a preset list, next and previous preset, a setlist.

Cons:
- No patching on the device. Every change needs the desktop and a card copy, or `scp`, which is much slower to iterate with.

- Patches are harder to debug without `MatrixView`. A read-only routing view would cost back part of the savings.

- Module state that is not a parameter is hard to write offline: MIDI device and CC assignments, and `gra4` and `loop` data. Today that state comes from an on-device save (the `state` field).

- Synthor saves rack's state in its own presets through `getStateInformation` (confirmed 2026-10-06, open question 1). That competes with the JSON file as the place a patch lives.

## 5. Option B: keep the editor

Pros:
- Small changes. Sections 2 and 3 add code and remove nothing.

- Patching and debugging on the device still work.

- `state` blobs are captured where they come from: save on the device, edit the rest on the desktop.

Cons:
- The 1353 lines of editing UI remain, written by someone else and now maintained by you.

- There are two ways to author a preset. A preset edited on the device diverges from the Python script that generated it.

- `MatrixView` shows one wire at a time, which does not scale to generated patches with many wires (section 6).

## 6. Routing screen (both options)

`MatrixView` shows one wire at a time: source module and channel, an arrow, destination, gain and offset. It picks a wire but gives no overview.

### Jack count

A jack is `Matrix::Jack`, a `(modIdx, chIdx)` pair: one channel of one module.

- Fixed: 10 nodes per track, 8 jacks on `IN` (`MAX_IO_IN`), 2 on `OUT` (`MAX_IO_OUT`).

- Variable: each slot's jacks come from the loaded module's descriptor. In `tools/py2rack/modules.json`, the most is 17 inputs (`vost`) and 16 outputs (`attn`, `vost`). 14 modules build their names at runtime, so their counts are unknown offline.

- Worst case per track: 136 sources x 138 destinations. On the 386 px high canvas that is under 3 px per cell, so a jack-level grid is not viable.

### Layout: module grid beside jack matrix (implemented, editable)

`Source/RoutingView.cpp`. On the track page, short Down opens it and Up returns. Long Down stays the global jump to the performance page.

rack's views, inherited from trax, are all drawn at the compact 640x480, even when Synthor hosts rack directly and gives its editor the full 1600x480 (`EditorHost.cpp`). The routing view is the only one laid out for both widths, from its own bounds:

| | Compact, 640 px | Full, 1600 px |
|-|-|-|
| Module grid | 26 x 38 px cells, 40 px row header | 38 x 38 px cells, 60 px row header |
| Jack matrix | rest of the width; labels cut at about 8 characters | 576 px: 100 px row labels, cells up to 28 px for 17 columns |
| Status | under the jack matrix | third column, top |
| Wire list | none | third column, every wire on the track in execution order |

The sizes below are for the compact layout; a to-scale drawing was used to choose them. The first device test showed the full screen 60% empty, which prompted the full layout.

```
  to  o d 3 4 5 6 7 8 O | [1] omod >   [2] drum
from  m r . . . . . . U |          A A S S A A S S H H H H
      o u               |          B B B B S S S S 1 1 2 2
IN    . . . . . . . . . | Main     . . . . . . . . # . . .
omod  . 3 . . . . . . . | Out A    . . . . # . . . . . . .
drum  . . . . . . . . 6 | Out B    # . . . . . . . . . . .
...                     | ...
                        | [3] Main ->  [4] HH1 Trig
                        |     x1.00 +0.00
```

- **Module grid:** rows are sources, columns destinations. IN has no inputs and OUT no outputs, so IN is only a row and OUT only a column: 9 x 9 cells. Cells are 26 x 38 px; the row header is 40 px for a 4-letter name, and column headers are rotated because a name does not fit across 26 px. A cell shows its wire count. Empty cells show a dot; a module's own cell is shaded.

- **Headers:** module names in a smaller font. An empty slot shows its number, dimmed. A module that appears twice on the track adds its slot number. Slot numbers are not used as labels otherwise: they read as indices into the grid, and the badges use numbers for encoders.

- **Colour:** green for forward wires, orange for wires that read the previous block, red for self-wires, which carry nothing. The colour comes from `rack::wireKind`, the same order the engine runs, and is tested against it. The jack matrix uses its module cell's colour.

- **Jack matrix:** about 330 px wide. Rows are the source module's outputs, columns the destination's inputs, with rotated column labels. A wired cell is filled, brighter with higher gain; repeated wires between the same jacks sum, as in the engine. Cells fill the area up to 28 px a side. The largest pair in `modules.json`, `omod` to `vost`, is 16 x 17 jacks and gets 15 x 15 px cells: selectable, but too small for text, so exact values go on the status line.

- **Status line:** the jack cursor as `[3] Main -> [4] HH1 Trig`, then its gain and offset, or "not wired". It replaced a list of the cell's wires; the jack matrix shows the same wires, plus every pair that could be wired.

- **Gain is the connection,** as in the file format, where a nonzero cell is a wire. `Level` switches encoders 3 and 4 to offset and gain, and the badges on the status line move to those values. Turning gain up from 0 adds the wire; turning it to 0 removes it (`Track::requestMatrixGain`). Pushing encoder 4 jumps between 0 and 1 (`Track::requestMatrixToggle`). Self-wires cannot be added.

- **Range on the device:** gain 0 to 1 in steps of 0.01, rounded so steps land exactly on 0. The engine and JSON accept any gain; a hand-written gain above 1 is clamped to 1 at its first edit on the device. Offset is unlimited and applies only to an existing wire.

An earlier version had a `Wire +/-` button to add and remove wires, separate from gain. It duplicated the encoder push, and it made "connected" a state the file format does not have. Before that, gain and offset were set by turning a held encoder, with a two-line hint that needed 26 characters in an 18-character pane.

Rows and columns are in execution order (section 3), not slot order. Forward wires lie right of the shaded cells; wires on or left of them are delayed or self-wires.

### Alternatives

| View | Pros | Cons |
|-|-|-|
| Signal flow: modules left to right in execution order, about 60 px each, wires as curves | Easiest to read for small patches | Cluttered past about 15 wires; wire layout is the most code |
| Wire list: one line per wire, JSON syntax (`1:Out L -> 2:In L x0.50`) | Least code; matches the file | No structure; about 16 wires per screen |
| Fixed patchbay: every slot has 16 or 17 fixed jack positions | Constant matrix shape, so fixed-size arrays for generation; cells never move | Swapping a module silently remaps signals (position 3 may be `In R` on one module, `Trig` on the next); most cells can never be used |

The grid keeps fixed dimensions at module level, where the user navigates. Jacks stay variable and named, so a wire cannot silently change meaning when a module is swapped.

## 7. Open questions

1. Does Synthor persist rack's state in its own presets? Yes: on 2026-10-06, `/media/BOOT/presets/009.pbp` and `013.pbp` held rack's binary state (the `TRAX` XML, with modules and wires), written through `getStateInformation`. Still open: which source wins at boot, the Synthor preset or a JSON file loaded afterwards. This decides whether a player needs any device save.

2. Should a player keep `ModuleView` for live tweaks, or rely on the performance page only?

3. How long does a preset switch take? `requestModuleChange` loads `.so` files. For a player, switching without gaps may matter more than anything else here. Measure before designing for it.

## 8. Recommendation

The intended end state is the player. The goal is many presets authored offline, which is what a player does. Each step below is useful even if the player never happens.

1. **Measure on the SSP** (open questions 1 and 3): preset switch time, and whether Synthor persists rack's state. These decide whether a player works.

2. **Graph-ordered execution** (section 3). Done.

3. **Routing grid** (section 6). Done; read-only version confirmed on the SSP 2026-10-06; editing not yet tried there.

4. **Matrix routing** in JSON presets, on the device and in `py2rack` (section 2). Done.

5. **Author on the desktop for a few weeks.** If on-device patching goes unused, delete the editing UI (option A); git keeps it recoverable.

Two results would change this:
- Preset switches take seconds. A player then needs modules preloaded or swapped without a gap, a larger engine project.

- Module state that is not a parameter can only be captured on the device, and presets depend on it. Then keep the editor (option B) and add a performance mode inside it.
