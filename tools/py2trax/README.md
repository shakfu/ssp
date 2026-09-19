# py2trax

Read and write trax presets from the command line.

trax loads and saves JSON itself, so writing a `.json` and copying it to the SSP needs no tool at all. The format is documented in `technobear/trax/README.md`. This tool adds one thing that only makes sense off the device:

- **Checking a preset before it gets there.** Module and channel names are verified against the plugin sources, so a typo is an error at your desk instead of a line in `dmesg`.

It also converts between JSON and trax's binary format, for builds predating the JSON loader. That half reads the byte layout and is expected to break; see Limits.

`examples/two_track/` holds one patch in both formats.

## Commands

    python3 py2trax.py scan ../../technobear -o modules.json   # module manifest
    python3 py2trax.py encode preset.json -o out -m modules.json
    python3 py2trax.py decode trax_presets/mypatch

`encode` takes the format trax reads and writes; the schema is in trax's README. `decode` prints the same schema, with channels as indices and parameters as ids, because that is what a preset file stores.

The manifest is what makes names checkable. Without `-m`, `encode` writes what it is given and checks nothing beyond the syntax.

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

None of this constrains trax. It resolves both against the loaded module, so every parameter and channel is addressable by name on the device.

## Limits

- Module names are not checked. The manifest lists what the sources build, not what is installed on the card.

- The binary format is not a specification. Upstream states that preset layout and module state may change between releases ([forum](https://forum.percussa.com/t/can-you-add-support-for-human-readable-presets-in-json/2084)), and `encode` and `decode` will break when they do. Prefer JSON, which trax resolves against the loaded module rather than against fixed offsets.

- `.filtergraph` is not a trax input. It is what JUCE's desktop plugin host writes when it saves a graph, with each module's state as a VST3 chunk. trax is an SSP module host, not a VST host; upstream's modules build as VSTs only as a development convenience. `decode` accepts a filtergraph because it makes a convenient test fixture.

## Tests

    make test        # from the repo root

Decoding is checked against `resources/test/trax.filtergraph`, a fixture holding a preset trax itself wrote: three modules, five wires, two performance parameters, and MIDI state for every module.
