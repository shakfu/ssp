# prdt

`prdt` runs a [Pure Data](https://puredata.info) patch (`.pd`) through [libpd](https://github.com/libpd/libpd). The patch gets eight audio inputs and outputs, sixteen controls, and MIDI. The SSP shows no patch GUI: edit patches in Pd on a computer, then copy them to the card.

## Install

Copy `prdt.so` to `BOOT/plugins` (from source: `make install MOD=prdt`, to a mounted card) and restart the SSP. See [Using](../../docs/USING.md#installing).

The example patches go in `BOOT/prdt`, where the browser opens. Pd's own abstractions from `extra/` (`rev1~`..`rev3~`, `hilbert~`, `output~`, `complex-mod~`) go in `BOOT/prdt/extra`, cyclone's `buffer~` in `BOOT/prdt/cyclone`, and ELSE's abstractions in `BOOT/prdt/else`. A release has them there. From source, `make install-prdt` copies them, to a mounted card or over ssh. Patches can sit anywhere on the card.

## First use

1. Add `prdt` and patch Out 1 and Out 2 to outputs. Synthor runs a module only while a jack is patched ([Using](../../docs/USING.md#a-module-runs-only-while-a-jack-is-patched)). The built-in saw plays.
2. Press Load (button 5), select `delay.pd`, and press Load again.
3. Patch audio to In 1.
4. Turn encoder 3 (`mix`) up to hear the echoes, then encoder 2 (`feedback`) for more of them.

## The language

### What it is

Pd is a visual dataflow language. A patch is a canvas of object boxes joined by wires. Objects ending in `~` process audio; the others pass messages. The file is text, one record per object or wire. See the [Pd manual](https://msp.ucsd.edu/Pd_documentation/) and its help patches.

### A first program

In 1 through a lowpass filter to Out 1, with its cutoff on encoder 1:

```text
[adc~ 1]     [r cutoff]
|            |
[lop~ 1000   ]
|
[dac~ 1]

@p1 cutoff 40 12000 Hz log      <- a comment in the patch
```

- `[adc~ 1]` reads In 1; `[dac~ 1]` writes Out 1.
- `[r cutoff]` receives encoder 1, in Hz, after a load and whenever it turns.
- The comment `@p1 cutoff 40 12000 Hz log` names encoder 1 and sets its range.

### How it maps to the SSP

| SSP | In Pd |
|-|-|
| Audio in | `[adc~ 1]` .. `[adc~ 8]` are In 1..In 8 |
| Audio out | `[dac~ 1]` .. `[dac~ 8]` are Out 1..Out 8 |
| Encoders | `[r name]` for a control declared `@pN name`; `[r p1]` .. `[r p16]` otherwise. [Controls](#controls) |
| CV in | `[adc~ N]`, 0.2 per volt; or `cv N` on an `@pN` comment |
| CV out | `[dac~ N]`, 0.2 per volt |
| Gates and triggers | in: `[threshold~]` on `[adc~ N]`; out: `[dac~ N]` at 1.0 (5 V) |
| MIDI in | `[notein]`, `[ctlin]`, `[bendin]`, `[pgmin]`, `[touchin]`, `[polytouchin]`. [MIDI](#midi) |
| MIDI out | n/a: `[noteout]` and the other outputs go nowhere |
| Clock and tempo | n/a: no clock from Synthor; patch one to an input |
| Sample rate and block size | `[samplerate~]` is the host's (48 kHz); Pd's block is 64 frames |
| Files on the card | abstractions, `[soundfiler]`, `[readsf~]` and `[text]` resolve next to the patch |
| Not available on the SSP | the patch GUI; externals other than those [built in](#objects) |

## Use

Load (button 5) opens a browser. Choose a `.pd` and press Load again. The screen shows the file and the last lines the patch printed. A load fails if any object cannot be created. The error names the object, and the previous patch keeps running. With no patch, a built-in saw plays, with controls pitch, level and cutoff.

A preset stores the patch's path and the encoder positions. Loading the preset opens the patch again.

## Controls

A comment in the patch names a control and gives its range:

```text
@p1 cutoff 20 20000 Hz log
@p2 mix
```

The screen then shows the name, the value in that range and the unit. `[r cutoff]` receives the value in that range. Each value goes out after a load, so after `[loadbang]`, and again whenever it changes. A control the patch does not declare goes to `[r p1]` .. `[r p16]` as 0..1.

[`@pN` lines](../../docs/script-editor.md#pn-lines) gives the form and its rules, including `cv N`. In Pd the line is a comment, with no `;` or `//` before it.

Receivers belong to one module: two `prdt` modules with `[r cutoff]` do not hear each other.

## MIDI

MIDI comes from the device chosen in the general panel (LS + RS, see [Using](../../docs/USING.md#the-general-panel)). All channel messages reach the patch, on their own channels. Once a note has arrived, the status panel counts them: `MIDI notes: 12`.

## Inputs and outputs

In 1..In 8 and Out 1..Out 8 carry audio and CV alike, at 0.2 per volt. Output is one Pd block (64 frames, 1.3 ms) late.

## Abstractions

An abstraction is a patch used as an object: `[twice~]` opens `twice~.pd`. Pd looks in the patch's folder first, then in `BOOT/prdt/lib` for your shared abstractions, then in `BOOT/prdt/extra`, `BOOT/prdt/cyclone` and `BOOT/prdt/else`. `BOOT/prdt` is also on the path, so `[else/name]` finds `BOOT/prdt/else/name.pd`. `[declare -path sub]` adds a folder relative to the patch.

## Objects

All Pd vanilla objects, including `[expr]` and `[expr~]`. Also, compiled in:

- Pd's `extra/` objects: `bob~`, `bonk~`, `choice`, `fiddle~`, `loop~`, `lrshift~`, `pique`, `sigmund~`, `stdout`. `pd~` is left out.
- [cyclone](https://github.com/porres/pd-cyclone) 0.9-5, the Max/MSP-compatible library: about 215 objects, such as `[coll]`, `[zl]`, `[counter]`, `[cycle~]` and `[svf~]`, by their plain names. Left out: `[loadmess]`, and `[mousestate]`, `[mousefilter]` and `[active]`, which read the mouse. `[scope~]` and `[comment]` create but draw nothing. `[coll]` reads and writes files on the thread its message arrives on, never on its own thread.
- [ELSE](https://github.com/porres/pd-else) 1.0-0 RC-15: about 370 objects, by their plain names and as `[else/name]`, and 305 abstractions. Ten names are also cyclone's: `atan2~`, `cartopol`, `cartopol~`, `pink~`, `poltocar`, `poltocar~`, `scope~`, `tanh`, `tanh~` and `trunc~`. The plain name is cyclone's object; `[else/pink~]` is ELSE's. Left out:
  - objects that need another library: `[plaits~]`, `[sfont~]`, `[sfz~]`, `[circuit~]`, `[beat~]`, `[streamin~]`, `[streamout~]`, `[pdlink]`, `[pdlink~]`, and the Lua objects such as `[display]`;
  - `[mouse]`, `[keycode]` and `[properties]`, which read the mouse or keyboard or open a dialog;
  - `[else]`, which prints the version.

  `[sfload]`, `[sfinfo]` and `[play.file~]` read WAV, AIFF, CAF and NeXT files with Pd's own reader, not ffmpeg: 16 and 24-bit integer and 32 and 64-bit float samples, but not MP3, OGG or FLAC, and `[sfload]` has no `download`. `[play.file~]` reads on a thread of its own and has no file panel.

  16 abstractions need one of the objects left out, among them `[sequencer]` and `[slider2d]`. GUI objects such as `[knob]` and `[note]` create but draw nothing. `[messcoll]` never uses its own thread.

Externals cannot load from the card. `prdt.so` keeps Pd's functions private, so a compiled external (`.pd_linux`) fails to link, and its object cannot be created.

## Examples

`examples/` holds patches to copy to `BOOT/prdt`. `make test` loads and plays each one.

| File | What | Controls |
|-|-|-|
| `delay.pd` | In 1 through a feedback delay to Out 1 and Out 2 | time, feedback, mix |
| `synth.pd` | mono MIDI synth: a saw through a lowpass; any note off ends the note | cutoff, release |
| `cv.pd` | sine on Out 1; In 1 is pitch CV, 1 V/oct above base | base |
| `arp.pd` | arpeggio on Out 1 and Out 2, from cyclone's `[counter]`, `[coll]` with embedded notes, `[cycle~]` and `[onepole~]` | rate, cutoff |
| `pluck.pd` | random notes from a pentatonic scale on Out 1 and Out 2, from ELSE's `[rand.i]` and `[pluck~]`, through its `[echo.rev~]` abstraction | rate, decay, size |

## Licence

The module is AGPL-3.0, as the repo is. libpd, Pure Data and cyclone are BSD-3-Clause. ELSE is mostly WTFPL-2; `conv~`, `fdn.rev~` and `giga.rev~` are GPL. [NOTICE.md](NOTICE.md) has the notices.
