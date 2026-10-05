# two_track

A two-track rack patch, and the files it was built from.

| file | what it is |
|-|-|
| `two_track.json` | the patch, hand-written; rack loads this directly |
| `two_track` | the same patch in rack's binary format, from `py2rack.py encode` |

The two presets describe the same patch by different routes, which makes them a differential test: load each and the resulting graph should be identical.

## The patch

Track 1 is a beat. `omod` runs as a square-wave LFO and its main and sub outputs trigger three `drum` voices:

| omod output | rate | drum voice |
|-|-|-|
| Main | 8 Hz | Hi Hat 1 |
| Out A (`Sub A Ratio` 0.5) | 4 Hz | Analog Snare |
| Out B (`Sub B Ratio` 0.25) | 2 Hz | Analog Bass Drum |

All three voices go to both track outputs. Everything else on `drum` stays at its default, which is enough to make sound.

`omod` keeps `Lfo` on, which multiplies `Freq` by 0.01 (`omod/Source/PluginProcessor.cpp`). `Freq` of 800 is therefore 8 Hz. `drum` triggers on a rising edge above 0.2, so a slower main oscillator gives correspondingly sparser hits: at `Freq` 50 the snare fires once every two seconds.

A drum voice only runs when both its trigger input and its output are wired. Connecting a trigger alone produces silence, not a silent voice.

Track 2 is a stereo `clds` on the track inputs, returning to the outputs at half gain.

Tracks 3 and 4 are empty.

Wires use channel names, so `"1:Main -> 2:HH1 Trig"` says what it does. Indices work too and mean the same thing.

## Parameters by name, and by id

`Freq`, `Wave` and `Lfo` are set by name. The sub-oscillator ratios are set by id (`slaveosc:0:ratio`), because `omod` builds those ids at runtime and `scan` cannot read them from the source.

rack itself has no such limit: it resolves names against the loaded plugin, so `Sub A Ratio` works when the JSON is loaded on the device. The ids are here so the same file also works through `encode`.

Channel names are the same story in reverse: `encode` checks them against the manifest, and every module in this patch declares its channels as a literal array, so all of them resolve.

## Rebuilding

From the repository root:

```sh
python3 tools/py2rack/py2rack.py encode \
    tools/py2rack/examples/two_track/two_track.json \
    -o tools/py2rack/examples/two_track/two_track \
    --modules tools/py2rack/modules.json
```

The manifest is what lets `encode` check the parameter and channel names. Drop `--modules` and nothing is checked, but the preset must then give every parameter as an id and every channel as an index.

## Using it

Copy either file into `rack_presets` at the root of the SSP's root filesystem. With the SD card mounted:

    sudo cp tools/py2rack/examples/two_track/two_track.json /media/sa/rootfs/rack_presets/
    sudo cp tools/py2rack/examples/two_track/two_track      /media/sa/rootfs/rack_presets/

Then load from rack's options page. rack decides by content, not by extension: a file starting with `{` is parsed as JSON, anything else as the binary format.

The filename is the preset name, so the binary one carries no extension.

## Reading it back

```sh
python3 tools/py2rack/py2rack.py decode tools/py2rack/examples/two_track/two_track
```

`decode` prints parameters by id, not by name, because a preset file stores ids. Comparing that output against `two_track.json` shows what the manifest resolved: `Freq` to `freq`, `Position` to `position`.
