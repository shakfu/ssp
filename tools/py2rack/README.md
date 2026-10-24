# py2rack

py2rack reads and writes [rack](../../plugins/rack/README.md) presets from the command line. rack loads and saves JSON itself, so a hand-written `.json` needs no tool. Format: [README-json-presets.md](../../plugins/rack/README-json-presets.md).

py2rack does three things:

- **check** a JSON preset's parameter and channel names against the plugin sources, so a typo fails on the desktop and not in `dmesg` on the SSP;
- **encode** JSON to the binary format, which rack shares with upstream trax;
- **decode** the binary format to JSON.

Encode and decode read the byte layout and will break when upstream changes it; see [Limits](#limits).

[examples/two_track](examples/two_track/README.md) holds one patch in both formats.

## Commands

From the repo root, where `<SSP>` is a checkout of [TheTechnobear/SSP](https://github.com/TheTechnobear/SSP):

    python3 tools/py2rack/py2rack.py scan <SSP>/technobear -o tools/py2rack/modules.json   # regenerate the manifest
    python3 tools/py2rack/py2rack.py encode preset.json -o mypatch -m tools/py2rack/modules.json -m tools/py2rack/modules-local.json
    python3 tools/py2rack/py2rack.py decode rack_presets/mypatch

`encode` takes the format rack reads and writes; the schema is in [README-json-presets.md](../../plugins/rack/README-json-presets.md#format). `decode` prints the same schema, with channels as indices and parameters as ids, because a preset file stores those.

The manifest (`-m`) maps names to ids and indices. Without `-m`, give every parameter as an id and every channel as an index. A channel given by name then fails: `cannot resolve slot 1 output 'Main': this module's channel names are not known`.

`modules-local.json` covers this repo's own modules (`edrm`, `gltc`, `rdio`, `chrs`, `fstr`, ...). Their channels and parameters are built at runtime, so a source scan cannot read them; `plugins/common/tests/test_engine_plugins.py` reads them from the built plugins instead, and fails when the file is stale (`UPDATE_MANIFEST=1 make test` rewrites it). Parameters are listed by id. `-m` can be given twice: `-m modules.json -m modules-local.json`.

`modules.json` is vendored: this repo no longer holds TheTechnobear's module sources. It was scanned from commit `7804388`. When upstream changes a module, rescan `<SSP>`.

## Matrix authoring

A track's routing can be a `matrix` field, which rack reads and writes itself; see [README-json-presets.md](../../plugins/rack/README-json-presets.md#format). `encode` accepts the field and checks its names like `wires`.

In Python, `matrix_wires` builds a track's `wires` from the same matrix. Rows are sources, columns destinations, and each nonzero cell becomes one wire:

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
- A `dc` row is a constant 1.0 source. Its weight becomes the `offset` of the first wire into that column. The engine adds each wire's offset, so one wire carries it. A `dc` entry in a column with no other wire is an error.
- Labels are checked for syntax. Channel names are checked by `encode -m`, as for hand-written wires.

`decode` reads the binary format, so it still prints `wires`.

## What the manifest knows

`scan` reads each module's sources for two things: its parameters, from the literal `ID::x, "Name"` form, and its channel names, from the `getInputBusName` / `getOutputBusName` arrays.

Neither is complete, because both are sometimes built at runtime:

| | covered | not covered |
|-|-|-|
| parameters | 135 | 137 (`getPID()`, per-layer groups) |
| channel names | 18 modules | 15 modules (`loop`, `gra4`, `pmix`, ...) |

The manifest records which modules it read incompletely, and `encode` uses that to decide how hard to be:

- A parameter key that is not a known name is an error for a module read completely, and is passed through as an id for one that was not. A typo in `clds` is caught; one in `omod` is not, because it cannot be told from a real runtime id such as `slaveosc:3:ratio`.

- A channel index past the end of a module's channels is an error. A channel name is an error for a module whose names are unknown, since there is nothing to match it against.

None of this constrains rack. It resolves both against the loaded module, so every parameter and channel is addressable by name on the device.

## Limits

- Module names are not checked. The manifest lists what the sources build, not what is installed on the card.

- The binary format is not a specification. Upstream states that preset layout and module state may change between releases ([forum](https://forum.percussa.com/t/can-you-add-support-for-human-readable-presets-in-json/2084)), and `encode` and `decode` will break when they do. Prefer JSON, which rack resolves against the loaded module rather than against fixed offsets.

- rack does not read `.filtergraph` files. JUCE's desktop plugin host writes them, with each module's state as a VST3 chunk. rack hosts SSP modules only; upstream builds its modules as VSTs for development. `decode` accepts a filtergraph for use as a test fixture.

## Tests

    make test        # from the repo root

Decoding is checked against `resources/test/trax.filtergraph`, a fixture holding a preset upstream trax wrote: three modules, five wires, two performance parameters, and MIDI state for every module.
