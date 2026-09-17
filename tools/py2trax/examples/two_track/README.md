# two_track

A two-track trax patch, and the files it was built from.

| file | what it is |
|-|-|
| `two_track.json` | the source description, hand-written |
| `two_track` | the generated preset, ready to copy to the SSP |
| `modules.json` | parameter manifest, from `py2trax.py scan technobear` |

## The patch

Track 1 reproduces the topology of the preset in `resources/test/trax.filtergraph`:
`omod` drives `drum` and `plts`, both of which reach the track output. `omod` has two parameters set by name, `Freq` and `Wave`.

Track 2 is a stereo `clds` on the track inputs, returning to the outputs at half gain.
All four of its named parameters are set.

Tracks 3 and 4 are empty.

## Rebuilding

From the repository root:

```sh
python3 tools/py2trax/py2trax.py scan technobear \
    -o tools/py2trax/examples/two_track/modules.json

python3 tools/py2trax/py2trax.py encode \
    tools/py2trax/examples/two_track/two_track.json \
    -o tools/py2trax/examples/two_track/two_track \
    --modules tools/py2trax/examples/two_track/modules.json
```

`modules.json` is only needed because the parameters are named. Drop `--modules` and give parameter ids instead, and the manifest is not consulted at all.

## Using it

Copy it into `trax_presets` at the root of the SSP's root filesystem. With the SD card
mounted:

    sudo cp tools/py2trax/examples/two_track/two_track /media/sa/rootfs/trax_presets/

Then load it from trax's options page. The filename is the preset name, so it carries no
extension.

## Reading it back

```sh
python3 tools/py2trax/py2trax.py decode tools/py2trax/examples/two_track/two_track
```

`decode` prints parameters by id, not by name, because a preset file stores ids. Comparing that output against `two_track.json` shows what the manifest resolved: `Freq` to `freq`, `Position` to `position`.
