# py2trax

Read and write trax presets from the command line.

trax loads a JSON preset directly (`PluginProcessor::loadJsonPreset`), so the usual route
is to write the `.json` and copy it to the SSP. This tool remains useful for two things:

- `decode`, which reads the binary presets trax saves, and the JUCE `.filtergraph` files
  the desktop plugin host writes.
- `encode`, which converts a JSON preset to that binary format, for a device running a
  trax build without the JSON loader.

Both formats describe the same patch, and `examples/two_track/` holds one of each.

## Commands

    python3 py2trax.py encode preset.json -o trax_presets/mypatch
    python3 py2trax.py encode preset.json -o out --modules modules.json   # parameters by name
    python3 py2trax.py decode trax_presets/mypatch                        # inspect, as JSON
    python3 py2trax.py scan ../../technobear -o modules.json              # parameter manifest

`decode` also reads a JUCE `.filtergraph`, so a patch saved from the desktop plugin host
can be dumped directly.

## Schema

```json
{
  "trax": 1,
  "tracks": {
    "1": {
      "level": 1.0,
      "mute": false,
      "modules": { "1": "clds", "2": "srvb" },
      "wires": [
        "in:0 -> 1:0",
        "1:0 -> 2:0",
        { "from": "2:0", "to": "out:0", "gain": 0.5, "offset": 0.0 }
      ],
      "params": { "1": { "Position": 25.0, "Size": 70.0 } }
    }
  },
  "performance": [ { "track": 1, "slot": "1", "param": 0 } ]
}
```

- `tracks` is keyed `"1"` to `"4"`, or given as a list. Omitted tracks are empty.
- `modules` is keyed by slot, `"1"` to `"8"`. Slot names `in` and `out` are the track's own
  input and output; they are built in and cannot hold a module.
- `wires` are `"<slot>:<channel> -> <slot>:<channel>"`. Channels are **0-based**, matching
  the stored `srcCh`/`destCh`. The object form adds `gain` and `offset`.
- `params` is keyed by slot, then by parameter name (needs `--modules`) or by parameter id.
  Values are in the parameter's own units, not 0-1. Omitted parameters keep the module's
  defaults, so a preset only has to name what it changes.
- Every field is optional. `{"trax": 1}` produces an empty four-track preset.

## Parameters by name

`scan` recovers name-to-id pairs from the plugin sources. It reads the literal
`ID::x, "Name"` form only: ids built at runtime (`getPID()`, per-layer groups) are counted
as skipped, not guessed. Coverage across the 32 modules is 134 named, 110 skipped.

Skipped parameters are still reachable by id, which `decode` prints. `omod`'s oscillator
ratios, for example, are `slaveosc:3:ratio`.

An unrecognised key is an error for a module the scan read completely, and is passed through
as an id for one it did not. The manifest records which is which, so a typo is still caught
wherever it can be.

This limit is an artifact of working offline, and applies to `encode` only. trax resolves
names against the loaded plugin through `parameterDesc`, so every parameter is reachable by
name when the JSON is loaded on the device.

## Limits

- Load-only. trax still saves in its own binary format; `decode` reads those back.
- `trax_presets` is resolved against the working directory of the process hosting trax
  (`OptionEditor.cpp`), not an absolute path. On the SSP, Synthor runs with cwd `/`, so the
  directory is `/trax_presets`.
- Module state beyond parameters is not written. Anything a module keeps outside its APVTS
  (MIDI assignments, file selections) is left at its default.
- Nothing is validated against the modules themselves. A wire to a channel the module does
  not have, or a module name that is not installed, fails on the SSP, not here.

## Tests

    make test

Decoding is checked against `resources/test/trax.filtergraph`, which holds a preset trax
itself wrote: three modules, five wires, two performance parameters.
