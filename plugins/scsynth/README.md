# scsynth

`scsy` runs a [SuperCollider](https://supercollider.github.io) SynthDef (`.scsyndef`) on the SSP, in scsynth 3.14.1. The SynthDef gets eight audio inputs and outputs and sixteen controls. Its UGens come from SuperCollider's core set and from [sc3-plugins](https://github.com/supercollider/sc3-plugins).

Design and measurements: [docs/dev/scsynth-module.md](../../docs/dev/scsynth-module.md).

## Install

1. Copy `scsy.so` to the `plugins` folder on the SD card. scsynth is linked in.
2. Copy the UGen plugins to `/media/BOOT/scsy/ugens`. `make install-scsy` does this, with the examples, to a card mounted on the host. A release has them under `BOOT/scsy`.

Without `ugens`, the module shows `no UGen directory /media/BOOT/scsy/ugens` and stays silent. The environment variable `SCSY_UGENS` names another folder, for tests and desktop builds. The first `scsy` in a session loads every plugin in `ugens`: about 0.4 s from the card.

## Use

Load (button 5) opens a browser at `/media/BOOT/scsy`. Choose a `.scsyndef` and press Load again. The screen shows the file, and the error if it does not load. A def that fails to load leaves the previous one running. With no def, a built-in drone plays: pitch, level and cutoff.

The preset stores the file's path, not its contents.

## The SynthDef

| | |
|-|-|
| audio and CV | see Inputs and outputs |
| controls | the def's named controls, mapped to `p1`..`p16`; see Controls |
| MIDI | notes; a def with a `gate` control plays a voice per note; see MIDI |
| UGens | any in `/media/BOOT/scsy/ugens`; a missing one fails the load with `UGen 'X' not installed` |

- The SSP sets the sample rate. scsynth runs in 64-frame blocks; the SSP's 128-frame block adds no latency.
- Only the file's first SynthDef runs.
- No buffers (`PlayBuf`, `GrainBufJ`, ... have nothing to read), no StkInst (it reads rawwave files from a path).

## Controls

Without a sidecar, `p1`..`p16` drive the def's first sixteen named controls in the def's order; nanosynth sorts them by name. A control's range is 0 to max(1, 2 x its default), or plus or minus that for a negative default.

A sidecar beside the def, `filter.txt` for `filter.scsyndef`, chooses the controls and scales them. It uses `csnd`'s lines; the label is the control's name:

```
// @p1 cutoff 20 20000 Hz log
// @p2 res
// @p3 mix
```

A label that names no control in the def fails the load. `cv N` at the end of a line makes input N move the control; see [CV in](#cv-in).

A new def starts with its defaults on every control; Load then sets the knobs to those defaults. A control the def reads only at the start, such as `DC`'s input, keeps its default for the synth's life.

## Inputs and outputs

Inputs 1..8 and outputs 1..8 carry audio and CV alike. An SSP signal of 1.0 is 5 V, so CV is 0.2 per volt: 1 V is 0.2, and 1 V/oct pitch is 0.2 per octave.

### Audio in

Buses 8..15: `In.ar(bus=NumOutputBuses.ir(), channel_count=8)`. Signals arrive unscaled, 1.0 for 5 V.

### CV in

A program gets CV in two ways: on a control, or by reading the input itself.

#### On a control

A control with CV follows input N (1..8): add `cv N` at the end of its sidecar line.

```
// @p1 cutoff 20 20000 Hz log cv 3
```

| Range | Each volt | -1 V | +1 V | +5 V |
|-|-|-|-|-|
| log | an octave | half | double | 32x |
| linear | a tenth of the range | -10% of the range | +10% | +50% |

- The encoder sets the value the CV moves from. The result stays within the control's range.
- The CV is applied at audio rate. The module fills an audio bus with the encoder's value moved by the CV, sample by sample, and maps the control to it. A control the def declares audio-rate follows every sample; a control-rate one reads every 64th (1.3 ms at 48 kHz). In nanosynth, `@synthdef("ar")` makes the first control audio-rate, as `fm` does for pitch.
- An orange mark on the control's bar shows where the CV has moved it, and the value shown is the moved one. The audio and the mark come from the same calculation. A preset stores the encoder position, not the moved value.
- The input still reaches the def as audio.

#### In the SynthDef

Read the input yourself, for a scaling of your own, such as no range limit. There is no mark on screen. 1 V/oct on input 3:

```python
freq = base * 2 ** (In.ar(bus=NumOutputBuses.ir() + 2) * 5)
```

### Audio out

Buses 0..7: `Out.ar(bus=0, source=...)`. `Out.kr` writes control buses, which do not reach the outputs.

### CV out

Write a value or a slow signal to an output, at 0.2 per volt. Use `Out.ar`, with `K2A` for a control-rate signal:

```python
Out.ar(bus=2, source=SinOsc.ar(frequency=2) * 0.5)    # output 3: +-2.5 V at 2 Hz
Out.ar(bus=3, source=K2A.ar(source=octaves * 0.2))    # output 4: 1 V/oct
```

## MIDI

Notes come from the MIDI input chosen in the general panel (RS + LS), and take effect at the next audio block. Once a note has arrived, the status panel shows how many have, and how many are held: `MIDI notes: 22, held: 16`.

A def with a `gate` control is a voice:

- A note on starts a synth with `freq` (Hz, from the note number), `velocity` (0..1) and `gate` 1. A note off sets `gate` 0.
- The def must free itself when its envelope ends: `EnvGen` with `done_action=DoneAction.FREE_SYNTH`.
- 16 notes sound at once; another releases the oldest held note. At most 32 voice synths exist, held or releasing; another frees the oldest, so a def that never frees itself cannot pile up.
- `p1`..`p16`, with their CV, drive every voice. Notes set `freq`, `velocity` and `gate`, so those stay off the encoders; a sidecar line naming one fails the load.
- Loading another def frees the voices.

```python
@synthdef()
def midi_saw(freq=440.0, velocity=0.5, gate=1.0, cutoff=2000.0):
    env = EnvGen.kr(envelope=Envelope.adsr(), gate=gate, done_action=DoneAction.FREE_SYNTH)
    sig = RLPF.ar(source=Saw.ar(frequency=freq), frequency=cutoff) * env * velocity
    Out.ar(bus=0, source=[sig, sig])
```

A def without `gate` runs one synth. A note sets its `freq` and `velocity`, if it has them and no encoder drives them: leave them out of the sidecar to play them from MIDI.

## Writing a SynthDef

Write it in Python with [nanosynth](https://github.com/shakfu/nanosynth), or in sclang, and compile it on a desktop:

```python
from nanosynth import synthdef
from nanosynth.ugens import In, NumOutputBuses, Out, RLPF

@synthdef()
def filter(cutoff=1000.0, res=0.3, mix=1.0):
    dry = In.ar(bus=NumOutputBuses.ir(), channel_count=2)
    wet = RLPF.ar(source=dry, frequency=cutoff, reciprocal_of_q=1.0 - res * 0.95)
    Out.ar(bus=0, source=dry * (1.0 - mix) + wet * mix)
```

```
nanosynth compile examples.py -o .
```

sc3-plugins UGens are in `nanosynth.ugens.sc3`. Copy the `.scsyndef`, and its sidecar if any, to `/media/BOOT/scsy`, on the mounted card or over the network: `scp -O filter.scsyndef root@192.168.1.6:/media/BOOT/scsy/` (see [docs/BUILDING.md](../../docs/BUILDING.md#network-access)).

## Examples

`examples/` holds the defs, their sidecars, and `examples.py`, their source.

| Def | UGens | Controls |
|-|-|-|
| `filter` | core | resonant lowpass on inputs 1-2: cutoff (CV on input 3), res, mix |
| `dfm1` | sc3-plugins (`DFM1`) | analogue-modelled filter on inputs 1-2: cutoff (CV on input 3), res (self-oscillates when high), gain (overdrive), type (low-pass below 0.5, else high-pass) |
| `lfo` | core | four LFOs at one rate as CV on outputs 1-4: sine, triangle, saw, square, +-depth V; input 1 is rate CV (an octave per volt), input 2 depth CV; controls rate, depth |
| `midi_saw` | core | MIDI voice, a saw through a resonant lowpass, to outputs 1-2: cutoff (CV on input 1), res, attack, release, level |
| `fm` | core | two-operator FM voice to outputs 1-2: pitch (1 V/oct CV on input 1, at audio rate), ratio, index (CV on input 2), level |

## Licence

GPL-3.0, for SuperCollider; the repo is AGPL-3.0.
