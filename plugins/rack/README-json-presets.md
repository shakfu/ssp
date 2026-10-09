# rack: JSON presets

rack reads and writes presets as JSON as well as in its own binary format. A JSON preset can be written by hand or generated, so a patch can be built on a desktop instead of slot by slot on the SSP.

rack is a fork of TheTechnobear's trax. Upstream trax has the binary format only. The binary format is unchanged, so binary presets move between the two.

## Loading

Presets live in `rack_presets`, reached from rack's options page. On the SSP that is `/media/BOOT/rack_presets`, on the SD card's FAT partition; elsewhere, `rack_presets` in the working directory.

Both formats sit in the same directory and the same file list. rack chooses by content, not by name: a file whose first non-whitespace character is `{` is read as JSON, anything else as the binary format. Presets written by older builds keep working.

A JSON preset is applied a track at a time, in this order: modules, `state`, parameters, then `wires` and `matrix`. Anything rack cannot apply is reported and skipped, and the rest of the preset still loads. Reports go to `/dev/kmsg`, so `dmesg` on the SSP shows them:

    json preset : track 1 : slot 2 has no input "Cowbell"
    json preset : track 1 slot 3 : no parameter named "Postion"
    json preset : track 2 slot 1 : cannot load module "clsd"

## Saving

Save from the options page as usual. A preset name ending in `.json` is written as JSON, any other name in the binary format.

The binary format stays the default because upstream trax cannot read JSON. Naming the preset is the whole opt-in; there is no mode to set.

A JSON save writes every parameter of every loaded module, by name, along with the modules, the routing as a `matrix`, track levels and the performance parameters. A saved preset reloads to the same patch. Wires at gain 0 are not saved; see the routing model in [README.md](README.md).

## Format

```json
{
  "rack": 1,
  "tracks": {
    "1": {
      "level": 1.0,
      "mute": false,
      "modules": { "1": "clds", "2": "srvb" },
      "matrix": {
        "rows": ["in:0", "1:Out L", "2:Out L"],
        "cols": ["1:In L", "2:In L", "out:0"],
        "gain": [[1, 0, 0  ],
                 [0, 1, 0  ],
                 [0, 0, 0.5]]
      },
      "params": { "1": { "Position": 25.0, "Size": 70.0 } }
    }
  },
  "performance": [ { "track": 1, "slot": "1", "param": 0 } ]
}
```

Every field is optional. `{"rack": 1}` loads an empty four-track preset.

**tracks** are keyed `"1"` to `"4"`, or given as a list. A track left out is empty.

**modules** are keyed by slot, `"1"` to `"8"`, and name the module to load: `clds` loads `/media/BOOT/plugins/clds.so`. The slots `in` and `out` are the track's own input and output. They are built in, hold 8 and 2 channels, and cannot take a module.

**matrix** is the track's routing as a gain matrix. `rows` are source jacks, `cols` destination jacks, and `gain[r][c]` is the gain of the wire from `rows[r]` to `cols[c]`. Each nonzero cell is a wire; name only the jacks you use. A row labelled `dc` is a constant 1.0 source: its weight is added to that column as an offset. A `dc` entry in a column with no wire into it is an error, and a malformed matrix is reported and skipped whole.

A jack is `"<slot>:<channel>"`. A channel is an index, 0-based, or one of the module's own channel names, so `"2:AS Trig"` and `"2:4"` are the same jack. Names are resolved against the loaded module, so they work for every module and are checked. A JSON save writes channel indices, which stay exact when a module repeats a name.

**wires** list the same routing one wire at a time, and are added to the matrix's wires. Each is `"<slot>:<channel> -> <slot>:<channel>"`. The object form, `{ "from": ..., "to": ..., "gain": 0.5, "offset": 0.0 }`, adds `gain` and `offset`, which default to 1 and 0. Presets saved before the matrix use `wires`, and still load.

Modules run in wiring order, not slot order. A wire on a feedback loop reads the previous block (128 samples); the loop is broken at its lowest slot. A wire from a module to itself carries nothing.

**params** are keyed by slot, then by parameter name or parameter id. Names are matched against the loaded module, case-insensitively as a fallback. Values are in the parameter's own units, not 0 to 1: `"Position": 25.0` on `clds` is 25 on a 0-100 range. A parameter left out keeps the module's default, so a preset only has to name what it changes.

**performance** lists the parameters shown on the performance page, by track, slot and parameter index.

**state**, described below, is optional and written by the JSON save.

## State that is not a parameter

A module can hold settings that are not parameters: its MIDI device and CC assignments, and whatever `gra4` and `loop` keep through `customToXml`. A preset carrying only parameters would drop them on the next save.

So a JSON save may add a `state` field, keyed by slot, holding base64 of the module's own state with its parameters stripped out:

```json
"state": { "1": "VkMyIf0CAAA8P3htbCB2ZXJzaW9u..." }
```

It is applied before the named parameters, which override it. Deleting a slot's `state` loses those settings and nothing else. A hand-written preset omits the field entirely.

`state` is the one field tied to JUCE. Named parameters go through `SSPExtendedApi::PluginInterface`, which is plain C++; `state` is a JUCE XML chunk taken from the module's own `getState`. A module that stops using JUCE loses the field, and nothing else in the preset.

## Implementation

| file | what it holds |
|-|-|
| `Source/JsonPreset.h/.cpp` | slot, jack and channel parsing; `matrix` parsing and formatting; format sniffing |
| `Source/Track.cpp` | `setStateInformation(var, int)` and `getStateInformation(var&)` |
| `Source/PluginProcessor.cpp` | `loadJsonPreset`, `saveJsonPreset`, and the dispatch in `loadPreset` / `savePreset` |

Loading goes through the same `requestModuleChange` and `requestMatrixConnect` calls as the binary path, so the handshake with the audio thread is unchanged. Parameters are read and written through `SSPExtendedApi::PluginInterface`, which is what makes them addressable by name.

## Building presets offline

`tools/py2rack/` reads and writes both formats from the command line: it converts a JSON preset to the binary format for upstream trax, decodes a binary preset to JSON, and checks module and channel names before a preset reaches the SSP. See its README.
