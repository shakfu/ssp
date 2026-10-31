# scsy

`scsy` runs a [SuperCollider](https://supercollider.github.io) SynthDef (`.scsyndef`) in scsynth 3.14.1. Its UGens (unit generators) come from SuperCollider's core set and from [sc3-plugins](https://github.com/supercollider/sc3-plugins).

## Install

Copy `scsy.so` to `BOOT/plugins` (from source: `make install MOD=scsy`, to a mounted card) and restart the SSP. See [Using](../../docs/USING.md#installing).

`BOOT/scsy` holds the examples and, in `ugens`, the UGen plugins. A release has them there. From source, `make install-scsy` copies both to a mounted card, or over ssh to `SSP_HOST`.

Without `ugens`, the module shows `no UGen directory /media/BOOT/scsy/ugens` and stays silent. An `scsy` added when no other is open loads every plugin in `ugens`: about 0.4 s from the card. The last `scsy` to close unloads them. A def that uses a missing UGen fails to load: `UGen 'X' not installed`.

## First use

1. Add `scsy` and patch Out 1 and Out 2 to outputs. Synthor runs a module only while a jack is patched ([Using](../../docs/USING.md#a-module-runs-only-while-a-jack-is-patched)). The built-in drone plays.
2. Press Load (button 5), select `filter.scsyndef`, and press Load again. Its controls start at the def's defaults: cutoff 1000 Hz, res 0.3, mix 1.
3. Patch stereo audio to In 1 and In 2.
4. Turn encoder 1 to sweep the cutoff, and encoder 2 for resonance.

## The language

### What it is

A SynthDef, or def, is a compiled synth graph. Its nodes are UGens: oscillators, filters, envelopes, bus readers and writers. sclang, SuperCollider's language, defines the graph. So does [nanosynth](https://github.com/shakfu/nanosynth), a Python library. Either compiles it to a `.scsyndef` file. The scsynth server runs the def. `scsy` runs compiled defs only: it has no sclang. See the [SynthDef reference](https://doc.sccode.org/Classes/SynthDef.html) and the [Getting Started tutorial](https://doc.sccode.org/Tutorials/Getting-Started/00-Getting-Started-With-SC.html).

### A first program

`filter` from `examples/examples.py`, in nanosynth, compiles to `examples/filter.scsyndef`:

```python
from nanosynth import synthdef
from nanosynth.ugens import In, NumOutputBuses, Out, RLPF

@synthdef()
def filter(cutoff=1000.0, res=0.3, mix=1.0):
    dry = In.ar(bus=NumOutputBuses.ir(), channel_count=2)
    wet = RLPF.ar(source=dry, frequency=cutoff, reciprocal_of_q=1.0 - res * 0.95)
    Out.ar(bus=0, source=dry * (1.0 - mix) + wet * mix)
```

Its sidecar, `examples/filter.txt`:

```text
// @p1 cutoff 20 20000 Hz log cv 3
// @p2 res
// @p3 mix
```

- `@synthdef()` makes the function a def named `filter`. Its arguments become controls, with their defaults.
- `In.ar(bus=NumOutputBuses.ir(), channel_count=2)` reads In 1 and In 2.
- `Out.ar(bus=0, ...)` writes Out 1 and Out 2.
- `// @p1 cutoff ...` puts `cutoff` on encoder 1, logarithmic, with CV from In 3.

[Writing a SynthDef](#writing-a-synthdef) compiles it and copies it to the card.

### How it maps to the SSP

| SSP | In a SynthDef |
|-|-|
| Audio in | `In.ar(bus=NumOutputBuses.ir())`: buses 8..15 are In 1..8; see [Audio in](#audio-in) |
| Audio out | `Out.ar(bus=0)`: buses 0..7 are Out 1..8; see [Audio out](#audio-out) |
| Encoders | the def's named controls, or the sidecar's `@pN` lines, become p1..p16; see [Controls](#controls) |
| CV in | `cv N` on a sidecar line; or `In.ar` on the input's bus; see [CV in](#cv-in) |
| CV out | `Out.ar`, with `K2A` for a control-rate signal; see [CV out](#cv-out) |
| Gates and triggers | `In.ar` as a `gate` or `trig` argument; MIDI notes set a `gate` control |
| MIDI in | a `gate` control makes a voice per note, with `freq` and `velocity`; see [MIDI](#midi) |
| MIDI out | n/a: `scsy` sends no MIDI |
| Clock and tempo | n/a: no host tempo; read clock pulses with `In.ar` |
| Sample rate and block size | `SampleRate.ir` is the SSP's rate; `BlockSize.ir` is 64, two per 128-frame SSP block |
| Files on the card | `.scsyndef` or [score](#scores) in `BOOT/scsy`; sidecar `.txt`; [buffers](#buffers); UGens in `BOOT/scsy/ugens` |
| Not available on the SSP | sclang; OSC from the network; `StkInst` (reads rawwave files); defs after the file's first, except in a [score](#scores) |

## Use

Load (button 5) opens a browser at `BOOT/scsy`. Choose a `.scsyndef` and press Load again. The screen shows the file, and the error if it does not load. A def that fails to load leaves the previous one running. With no def, a built-in drone plays, with controls pitch, level and cutoff.

The preset stores the file's path, not its contents. A def is a compiled binary, so `scsy` has no Code view and no key port.

## Controls

Without a sidecar, `p1`..`p16` drive the def's first sixteen named controls in the def's order. [nanosynth](https://github.com/shakfu/nanosynth), a Python library that writes SynthDefs, sorts them by name. A control's range is 0 to max(1, 2 x its default), or plus or minus that for a negative default.

A sidecar beside the def, `filter.txt` for `filter.scsyndef`, chooses the controls and scales them. Each line starts with `//` and takes the [`@pN` form](../../docs/script-editor.md#pn-lines); the label is the control's name:

```text
// @p1 cutoff 20 20000 Hz log
// @p2 res
// @p3 mix
```

A label that names no control in the def fails the load. `cv N` at the end of a line makes input N move the control; see [CV in](#cv-in).

A new def starts with its defaults on every control; Load then sets the encoders to those defaults. A control the def reads only at the start, such as `DC`'s input, keeps its default for the synth's life.

## Inputs and outputs

In 1..In 8 and Out 1..Out 8 carry audio and CV alike, at 0.2 per volt; see [CV](../../docs/script-editor.md#cv).

### Audio in

`In.ar(bus=NumOutputBuses.ir(), channel_count=8)` reads all eight. Signals arrive unscaled, 1.0 for 5 V.

### CV in

On a control, add `cv N` at the end of its sidecar line; see [CV on a control](../../docs/script-editor.md#cv-on-a-control).

```text
// @p1 cutoff 20 20000 Hz log cv 3
```

- The CV applies at audio rate to an audio-rate control, and every 64 samples (1.3 ms at 48 kHz) to a control-rate one. In nanosynth, `@synthdef("ar")` makes the first control audio-rate, as `fm` does for pitch.

- The audio and the orange mark come from the same calculation.

In the SynthDef, read the input yourself, for a scaling such as no range limit. 1 V/oct on input 3:

```python
freq = base * 2 ** (In.ar(bus=NumOutputBuses.ir() + 2) * 5)
```

### Audio out

`Out.kr` writes control buses, which do not reach the outputs.

### CV out

Use `Out.ar`, with `K2A` for a control-rate signal:

```python
Out.ar(bus=2, source=SinOsc.ar(frequency=2) * 0.5)    # output 3: +-2.5 V at 2 Hz
Out.ar(bus=3, source=K2A.ar(source=octaves * 0.2))    # output 4: 1 V/oct
```

## MIDI

Notes come from the MIDI input chosen in the general panel (LS + RS, see [Using](../../docs/USING.md#the-general-panel)), and take effect at the next audio block. Once a note has arrived, the status panel shows how many have, and how many are held: `MIDI notes: 22, held: 16`.

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

Write it in Python with nanosynth, or in sclang, and compile it on a desktop. For the [first program](#a-first-program), in `examples/`:

```text
nanosynth compile examples.py -o .
```

sc3-plugins UGens are in `nanosynth.ugens.sc3`. Copy the `.scsyndef`, and its sidecar if any, to `BOOT/scsy`, on the mounted card or over the network: `scp -O filter.scsyndef root@<ssp-address>:/media/BOOT/scsy/` (see [docs/BUILDING.md](../../docs/BUILDING.md#network-access)).

## Examples

`examples/` holds the defs, their sidecars, and `examples.py`, their source.

| Def | UGens | Controls |
|-|-|-|
| `filter` | core | resonant lowpass on inputs 1-2: cutoff (CV on input 3), res, mix |
| `dfm1` | sc3-plugins (`DFM1`) | analogue-modelled filter on inputs 1-2: cutoff (CV on input 3), res (self-oscillates when high), gain (overdrive), type (low-pass below 0.5, else high-pass) |
| `lfo` | core | four LFOs at one rate as CV on outputs 1-4: sine, triangle, saw, square, +-depth V; input 1 is rate CV (an octave per volt), input 2 depth CV; controls rate, depth |
| `midi_saw` | core | MIDI voice, a saw through a resonant lowpass, to outputs 1-2: cutoff (CV on input 1), res, attack, release, level |
| `fm` | core | two-operator FM voice to outputs 1-2: pitch (1 V/oct CV on input 1, at audio rate), ratio, index (CV on input 2), level |

## Buffers

The sidecar names buffers, and `scsy` fills them before the synth starts. The def reads them by number, as `PlayBuf.ar(buffer_id=0, ...)`:

```text
// @buf 0 samples/loop.wav
// @buf 1 alloc 10 2
```

- `@buf N path`: a sound file libsndfile reads, such as WAV, AIFF or FLAC. A relative path is relative to the def's folder.

- `@buf N alloc SECONDS [CHANNELS]`: an empty buffer, for recording. CHANNELS defaults to 1.

- N is 0..63. A file that cannot be opened fails the load, and the running def keeps playing.

- A reload skips a line that has not changed, file and modification time included, so a recording in an `alloc` buffer survives it. A number the new def no longer names is freed once it runs.

- At most 256 MB of buffers, as 32-bit samples: about 23 minutes of mono at 48 kHz. Over that, the load fails before anything is read.

- scsynth does not resample: play a file recorded at another rate with `BufRateScale`.

- A rate change reopens scsynth, which reloads the def and its buffers; `alloc` buffers lose their recordings.

## Scores

A score is an NRT command file (scsynth's non-real-time format): timed bundles of OSC commands. [sapf-sc](https://github.com/shakfu/sapf-sc)'s `>nrt` writes one for a program whose `ola` starts a synth per sound. Load a score as a def; its sidecar has the same name, `texture.txt` for `texture.osc`.

- The first `/s_new` starts the main synth. Its def takes `p1`..`p16`, the sidecar and MIDI as a `.scsyndef` would.

- Every later `/s_new` starts a synth at its bundle's time, in a group before the main synth, so the main synth can read the buses they write. scsynth starts each on the 64-frame block that holds its time.

- `/d_recv` and buffer commands (`/b_...`) run when the score loads, wherever they are in the file. A relative path in `/b_allocRead` is relative to the score's folder. The sidecar's `@buf` lines work as for a def.

- The score loops at the time of its last bundle.

- `/d_free`, `/c_set` and `/g_freeAll` are ignored; any other command fails the load.

- At most 900 SynthDefs besides the main one.

## Known issues

Adding or deleting a module crashed Synthor once in 26 cycles, with no trace. Synthor restarts by itself; save presets first.

## Licence

GPL-3.0, for SuperCollider; the repo is AGPL-3.0.
