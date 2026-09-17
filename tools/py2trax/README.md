# py2trax

Read and write trax presets from the command line.

trax loads and saves JSON itself, so writing a `.json` and copying it to the SSP needs no tool at all. The format is documented in `technobear/trax/README.md`. This tool adds three things that only make sense off the device:

- **Checking a preset before it gets there.** Module and channel names are verified against the plugin sources, so a typo is an error at your desk instead of a line in `dmesg`.

- **Converting to the binary format**, for a trax build without the JSON loader.

- **Reading a binary preset**, including the JUCE `.filtergraph` files the desktop plugin host writes.

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

- `decode` on a `.filtergraph` reads the state JUCE's plugin host saved, which is the same preset wrapped in a VST3 chunk.

## Tests

    make test

Decoding is checked against `resources/test/trax.filtergraph`, which holds a preset trax itself wrote: three modules, five wires, two performance parameters, and MIDI state for every module.
