# two_track

A two-track rack patch, and the files it was built from.

| file | what it is |
|-|-|
| `two_track.json` | the patch, hand-written; rack loads this directly |
| `two_track` | the same patch in rack's binary format, from `py2rack.py encode` |

Both files describe the same patch. Loading either should give the same graph, so a difference between them points to a bug in `encode` or in rack.

## The patch

Track 1 is a beat. `omod` runs as a square-wave LFO and its main and sub outputs trigger three `drum` voices:

| omod output | rate | drum voice |
|-|-|-|
| Main | 8 Hz | Hi Hat 1 |
| Out A (`Sub A Ratio` 0.5) | 4 Hz | Analog Snare |
| Out B (`Sub B Ratio` 0.25) | 2 Hz | Analog Bass Drum |

All three voices go to both track outputs. Everything else on `drum` stays at its default, which is enough to make sound.

`omod` keeps `Lfo` on, so `Freq` 800 is 8 Hz ([omod as a clock](../../../../presets/README.md#omod-as-a-clock)). `drum` triggers on a rising edge above 0.2. A slower main oscillator gives fewer hits: at `Freq` 50 the snare fires once every two seconds.

A drum voice runs only when its trigger input and its output are both wired.

Track 2 is a stereo `clds` on the track inputs, returning to the outputs at half gain.

Tracks 3 and 4 are empty.

Wires use channel names, so `"1:Main -> 2:HH1 Trig"` says what it does. Indices work too and mean the same thing.

## Parameters by name, and by id

`Freq`, `Wave` and `Lfo` are set by name. The sub-oscillator ratios are set by id (`slaveosc:0:ratio`), because `omod` builds those ids at runtime and `scan` cannot read them from the source.

rack itself has no such limit: it resolves names against the loaded plugin, so `Sub A Ratio` works when the JSON is loaded on the device. The ids are here so the same file also works through `encode`.

`encode` checks channel names against the manifest. Every module in this patch declares its channels as a literal array, so all names resolve.

## Rebuilding

From the repository root:

```sh
python3 tools/py2rack/py2rack.py encode \
    tools/py2rack/examples/two_track/two_track.json \
    -o tools/py2rack/examples/two_track/two_track \
    --modules tools/py2rack/modules.json
```

The manifest lets `encode` check parameter and channel names. Without `--modules`, give every parameter as an id and every channel as an index.

## Using it

Copy either file to `BOOT/rack_presets` and load it from rack's options page; see [Loading](../../../../plugins/rack/README-json-presets.md#loading). The filename is the preset name, so the binary one has no extension.

## Reading it back

```sh
python3 tools/py2rack/py2rack.py decode tools/py2rack/examples/two_track/two_track
```

`decode` prints parameters by id, not by name, because a preset file stores ids. Comparing that output against `two_track.json` shows what the manifest resolved: `Freq` to `freq`, `Position` to `position`.
