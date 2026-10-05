# py2rack

Read and write rack presets from the command line.

rack loads and saves JSON itself, so writing a `.json` and copying it to the SSP needs no tool at all. The format is documented in `plugins/rack/README-json-presets.md`. This tool adds one thing that only makes sense off the device:

- **Checking a preset before it gets there.** Module and channel names are verified against the plugin sources, so a typo is an error at your desk instead of a line in `dmesg`.

It also converts between JSON and the binary format, which rack shares with upstream trax. That half reads the byte layout and is expected to break; see Limits.

`examples/two_track/` holds one patch in both formats.

## Commands

    python3 py2rack.py scan <SSP>/technobear -o modules.json   # regenerate the manifest
    python3 py2rack.py encode preset.json -o out -m modules.json
    python3 py2rack.py decode rack_presets/mypatch

`encode` takes the format rack reads and writes; the schema is in rack's README. `decode` prints the same schema, with channels as indices and parameters as ids, because that is what a preset file stores.

The manifest is what makes names checkable. Without `-m`, `encode` writes what it is given and checks nothing beyond the syntax.

`modules.json` is vendored: this repo no longer holds TheTechnobear's module sources. It was scanned from commit `7804388`. When upstream changes a module, rescan a checkout of [TheTechnobear/SSP](https://github.com/TheTechnobear/SSP).

## Matrix authoring

`matrix_wires` builds a track's `wires` from a gain matrix. Rows are sources, columns destinations, and each nonzero cell becomes one wire:

```python
import py2rack

wires = py2rack.matrix_wires(
    rows=["in:0", "in:1", "1:Out L", "dc"],
    cols=["1:In L", "1:In R", "out:0"],
    gain=[[1, 0, 0  ],
          [0, 1, 0  ],
          [0, 0, 0.5],
          [0, 0, 0.1]],
)
# ['in:0 -> 1:In L', 'in:1 -> 1:In R', {'from': '1:Out L', 'to': 'out:0', 'gain': 0.5, 'offset': 0.1}]
```

- `gain` is any 2-D sequence, including a numpy array. Name only the jacks you use.
- A `dc` row is a constant 1.0 source. Its weight becomes the `offset` of the first wire into that column, since the engine adds each wire's offset. A `dc` entry in a column with no other wire is an error.
- Labels are checked for syntax. Channel names are checked by `encode -m`, as for hand-written wires.

The JSON format has no matrix field; the result is an ordinary `wires` list. See `docs/dev/rack-design.md`, section 2.

## What the manifest knows

`scan` reads each module's sources for two things: its parameters, from the literal `ID::x, "Name"` form, and its channel names, from the `getInputBusName` / `getOutputBusName` arrays.

Neither is complete, because both are sometimes built at runtime:

| | covered | not covered |
|-|-|-|
| parameters | 134 | 110 (`getPID()`, per-layer groups) |
| channel names | 18 modules | 14 modules (`loop`, `gra4`, `pmix`, ...) |

The manifest records which modules it read incompletely, and `encode` uses that to decide how hard to be:

- A parameter key that is not a known name is an error for a module read completely, and is passed through as an id for one that was not. A typo in `clds` is caught; one in `omod` is not, because it cannot be told from a real runtime id such as `slaveosc:3:ratio`.

- A channel index past the end of a module's channels is an error. A channel name is an error for a module whose names are unknown, since there is nothing to match it against.

None of this constrains rack. It resolves both against the loaded module, so every parameter and channel is addressable by name on the device.

## Limits

- Module names are not checked. The manifest lists what the sources build, not what is installed on the card.

- The binary format is not a specification. Upstream states that preset layout and module state may change between releases ([forum](https://forum.percussa.com/t/can-you-add-support-for-human-readable-presets-in-json/2084)), and `encode` and `decode` will break when they do. Prefer JSON, which rack resolves against the loaded module rather than against fixed offsets.

- `.filtergraph` is not a rack input. It is what JUCE's desktop plugin host writes when it saves a graph, with each module's state as a VST3 chunk. rack is an SSP module host, not a VST host; upstream's modules build as VSTs only as a development convenience. `decode` accepts a filtergraph because it makes a convenient test fixture.

## Tests

    make test        # from the repo root

Decoding is checked against `resources/test/trax.filtergraph`, a fixture holding a preset upstream trax wrote: three modules, five wires, two performance parameters, and MIDI state for every module.
