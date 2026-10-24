# csnd

`csnd` runs a [Csound 7](https://csound.com) orchestra (`.csd`). The orchestra gets eight audio inputs and outputs, sixteen controls, and MIDI.

Ported from the [`csound` engine](https://github.com/shakfu/sk-engines/tree/main/src/engine/csound) in [sk-engines](https://github.com/shakfu/sk-engines).

## Install

Copy `csnd.so` to `BOOT/plugins` (from source: `make install MOD=csnd`, to a mounted card) and restart the SSP. See [Using](../../docs/USING.md#installing).

The example orchestras go in `BOOT/csound`, where the browser opens. A release has them there. From source, copy `examples/` to `BOOT/csound` by hand. Orchestras can sit anywhere on the card.

A USB keyboard edits the code; it is optional ([Keyboard](#keyboard)).

## First use

1. Add `csnd` and patch Out 1 and Out 2 to outputs. Synthor runs a module only while a jack is patched ([Using](../../docs/USING.md#a-module-runs-only-while-a-jack-is-patched)). The built-in drone plays.
2. Press Load (button 5), select `filter.csd`, and press Load again.
3. Patch stereo audio to In 1 and In 2.
4. Turn encoder 4 (`mix`) up to hear the filter, then encoder 1 to sweep the cutoff.

[Walkthrough: `filter.csd`](#walkthrough-filtercsd) explains each step.

## The language

### What it is

A Csound program is a `.csd` file holding an orchestra and an optional score. The orchestra, in `<CsInstruments>`, defines instruments as `instr` ... `endin` blocks. The score, in `<CsScore>`, lists events that start instruments. Instruments compute `a` variables at audio rate and `k` variables once per `ksmps` samples. See the [reference manual](https://csound.com/docs/manual/index.html) and, for beginners, the [Csound FLOSS Manual](https://flossmanual.csound.com/).

### A first program

A mono lowpass filter from In 1 to Out 1, with its cutoff on encoder 1:

```csound
<CsoundSynthesizer>
<CsInstruments>
; @p1 cutoff 40 20000 Hz log
nchnls = 1
0dbfs = 1
massign 0, 0             ; ignore MIDI

instr 1
  ain  inch 1            ; In 1
  kcut chnget "p1"       ; encoder 1, in Hz
  outch 1, moogladder(ain, kcut, 0.5)
endin
schedule 1, 0, -1
</CsInstruments>
</CsoundSynthesizer>
```

- `; @p1 cutoff 40 20000 Hz log` names encoder 1 and sets its range.
- `chnget "p1"` reads that control as a k-rate value in Hz.
- `inch 1` reads In 1; `outch 1` writes Out 1.
- `schedule 1, 0, -1` starts instrument 1 now, with no end.

### How it maps to the SSP

| SSP | In Csound |
|-|-|
| Audio in | `inch 1` .. `inch 8` are In 1..In 8; also `ins`. [Audio in](#audio-in) |
| Audio out | `outch 1` .. `outch 8` are Out 1..Out 8; also `outs`. [Audio out](#audio-out) |
| Encoders | `chnget "p1"` .. `chnget "p16"`, set once per audio block. [Labels and ranges](#labels-and-ranges) |
| CV in | `cv N` on an `@pN` line, or `inch N` / 0.2 for volts. [CV in](#cv-in) |
| CV out | `outch N`, 0.2 per volt with `0dbfs = 1`. [CV out](#cv-out) |
| Gates and triggers | in: `trigger` on `k(inch(N))`; out: `outch N` with 1.0 (5 V) |
| MIDI in | `massign`, `notnum`, `veloc`, `cpsmidi`; notes only, channel 1. [The orchestra](#the-orchestra) |
| MIDI out | n/a: the module passes no MIDI from the orchestra to the host |
| Clock and tempo | n/a: no clock from Synthor; patch one to an input. Score `t` sets score tempo |
| Sample rate and block size | `sr` is the host's (48 kHz); `ksmps` is the orchestra's. [The orchestra](#the-orchestra) |
| Files on the card | GEN01, `diskin`, `#include` paths resolve next to the `.csd` |
| Not available on the SSP | console output (`prints`, `printks`); MIDI out |

## Use

Load (button 5) opens a browser. Choose a `.csd` and press Load again. The screen shows the file, and the compile error if there is one. A program that fails to compile leaves the previous one running. With no program, a built-in drone plays, with controls pitch, level and cutoff. MIDI notes play a separate plucked voice through the same cutoff.

A preset stores the program as [Script modules](../../docs/script-editor.md#presets) describes.

## Keyboard

The [shared editor](../../docs/script-editor.md) edits the orchestra, on key port 7773. Ctrl-S on the built-in writes `BOOT/csound/live.csd`. Ctrl-., Ctrl-R and Ctrl-1..0 do nothing.

Ctrl-E runs the block around the cursor in the running orchestra. A block inside `<CsScore>` goes as score events, such as `i 2 0 4`. Elsewhere it compiles the block as orchestra code: `schedule 2, 0, 4` plays, `instr 2 ... endin` replaces instrument 2, and `gkx = 0.5` sets a global.

- A replaced instrument applies to notes that start after it. A held note keeps the old definition until it ends.
- A block ends at a blank line. Keep an instrument free of blank lines to send it whole.
- A held note on the same instrument number ties to the running one. Use a fractional number, such as `i 2.1`, for a second voice.
- Errors in an event show only when it plays, not on Ctrl-E.

## The orchestra

- The SSP sets `sr`; the orchestra's own `sr` is ignored.

- `ksmps` is the orchestra's. Output is one k-cycle late.

- MIDI comes from the device chosen in the general panel (LS + RS, see [Using](../../docs/USING.md#the-general-panel)). Notes arrive on channel 1. Csound starts `instr n` for MIDI channel `n` unless told otherwise, so an orchestra that ignores MIDI needs `massign 0, 0`. Once a note has arrived, the status panel counts them: `MIDI notes: 12`.

## Labels and ranges

A comment line names a control and gives its range; the screen then shows the name, the value in that range and the unit, and the program receives the value in that range:

```text
; @p1 cutoff 20 20000 Hz log
; @p2 mix
```

The line starts with `;`. [`@pN` lines](../../docs/script-editor.md#pn-lines) gives the form and its rules.

## Inputs and outputs

In 1..In 8 and Out 1..Out 8 carry audio and CV alike, at 0.2 per volt; see [CV](../../docs/script-editor.md#cv).

### Audio in

`inch 1` .. `inch 8`, or `ins`; `nchnls_i` sets how many. Signals are scaled by `0dbfs`: a full-scale SSP signal (1.0) reads as `0dbfs`, so with `0dbfs = 1` a value reads as on the SSP.

### CV in

On a control, add `cv N` at the end of its `@pN` line; see [CV on a control](../../docs/script-editor.md#cv-on-a-control). The CV is read once per audio block: 128 frames, 2.7 ms at 48 kHz. That suits envelopes, LFOs and sequencers, not audio-rate modulation.

```text
; @p1 cutoff 20 20000 Hz log cv 3
```

In the orchestra, read the input yourself. `examples/filter.csd` reads In 3 at k-rate and multiplies the cutoff by `2 ^` its volts:

```text
kcv = k(inch(3)) / 0.2        ; SSP CV is 0.2 per volt
```

[Walkthrough step 6](#6-add-cv) explains it.

### Audio out

`outch 1` .. `outch 8`, or `outs`; `nchnls` sets how many. The same `0dbfs` scaling applies.

### CV out

With `0dbfs = 1` and `nchnls` at least 4:

```text
aLfo oscili 0.5, 2                ; +-2.5 V at 2 Hz
outch 3, aLfo, 4, a(0.2 * kOct)   ; Out 3: the LFO; Out 4: 1 V/oct, kOct octaves
```

## Examples

`examples/` holds orchestras to copy to `BOOT/csound`. `make test` compiles and runs each one.

| File | What | Controls |
|-|-|-|
| `filter.csd` | stereo ladder filter on inputs 1-2; input 3 is cutoff CV, 1 V/oct | cutoff, resonance, drive, mix |
| `delay.csd` | stereo ping-pong delay on inputs 1-2 | time, feedback, mix, tone |
| `quadosc.csd` | sine, triangle, saw and pulse on outputs 1-4; input 1 is pitch CV (1 V/oct), input 2 width CV, both declared with `cv` | pitch, width |
| `lfo.csd` | four LFOs at one rate as CV on outputs 1-4: sine, triangle, saw, square, +-depth V; input 1 is rate CV (an octave per volt), input 2 depth CV | rate, depth |
| `fm-midi.csd` | polyphonic FM synth, played over MIDI; silent until notes arrive from the input chosen in the general panel | ratio, index, attack, release |
| `xanadu.csd` | Joseph T. Kung's *Xanadu* (1988), plucked strings through delays, 1 min; from Csound's `examples/` (LGPL-2.1) | none |
| `trapped.csd` | Richard Boulanger's *Trapped in Convert* (1979, revised 1986), about 4 min 50 s; from Csound's `examples/` (LGPL-2.1) | none |

`xanadu.csd` and `trapped.csd` are the Csound 7.0.0 files. Both add `massign 0, 0`: they play their score once and ignore MIDI.

## Walkthrough: `filter.csd`

`examples/filter.csd` is a stereo ladder filter. It extends [A first program](#a-first-program) with resonance, drive, mix and cutoff CV. This follows it from the card to the screen.

```csound
; Stereo ladder filter on inputs 1 and 2; input 3 is cutoff CV, 1 V/oct.
; @p1 cutoff 40 20000 Hz log
; @p2 resonance
; @p3 drive 1 9
; @p4 mix
ksmps = 32
nchnls = 2
nchnls_i = 3
0dbfs = 1
massign 0, 0                ; no MIDI: by default channel n starts instr n

instr 1
  kcv   = k(inch(3)) / 0.2              ; SSP CV is 0.2 per volt
  kfreq limit chnget:k("p1") * 2 ^ kcv, 20, sr * 0.45
  kfreq port kfreq, 0.02
  kres  chnget "p2"
  kgain chnget "p3"
  kmix  chnget "p4"
  aL inch 1
  aR inch 2
  afL moogladder tanh(aL * kgain), kfreq, kres
  afR moogladder tanh(aR * kgain), kfreq, kres
  outs aL + (afL - aL) * kmix, aR + (afR - aR) * kmix
endin
schedule 1, 0, -1
```

### 1. Copy it to the card

Put `filter.csd` in `BOOT/csound`. The release already has it there.

### 2. Patch it in Synthor

`csnd` always has eight inputs, `In 1` to `In 8`, and eight outputs, `Out 1` to `Out 8`. `nchnls_i = 3` and `nchnls = 2` say how many the orchestra uses:

| In the orchestra | `csnd` jack | Patch |
|-|-|-|
| `inch 1`, `inch 2` | In 1, In 2 | stereo audio to filter |
| `inch 3` | In 3 | cutoff CV, 1 V/oct |
| `outs` | Out 1, Out 2 | to the next module or an output |

### 3. Load it

Press Load (button 5). The browser opens at `BOOT/csound`. Turn encoder 1, or press Up and Down, to select `filter.csd`. Left and Right (`PG-`, `PG+`) jump a column, and pressing encoder 1 opens a folder. Press Load again. Cancel (button 7) leaves without loading.

### 4. Read the screen

```text
csnd : Csound 7 host                                  1/1 controls     DSP n.n% pk n.n%
   cutoff       resonance       drive          mix    filter.csd
  40.0 Hz         0.000         1.00          0.000
```

Each `@p` line became one control, in order, four to a page:

| Declaration | Encoder | Screen | Range | The orchestra reads |
|-|-|-|-|-|
| `; @p1 cutoff 40 20000 Hz log` | 1 | `cutoff`, in Hz | 40 to 20000, logarithmic | `chnget "p1"` |
| `; @p2 resonance` | 2 | `resonance` | 0 to 1 | `chnget "p2"` |
| `; @p3 drive 1 9` | 3 | `drive` | 1 to 9 | `chnget "p3"` |
| `; @p4 mix` | 4 | `mix` | 0 to 1 | `chnget "p4"` |

The right panel shows the page (`1/1 controls`), the DSP load, the file name, and a compile error in orange if there is one. Four controls fit one page; a program that declares five to eight gets a second page, and Up and Down move between pages.

### 5. Play it

Every control starts at the bottom of its range, so `mix` is 0 and you hear the dry input. Turn encoder 4 up to hear the filter, then encoder 1 to sweep the cutoff.

[Encoders](../../docs/script-editor.md#encoders) gives the step sizes. `cutoff` spans about 9 octaves, so a detent is about half an octave.

### 6. Add CV

The orchestra multiplies the cutoff by `2 ^ (In 3 / 0.2)`: each volt on In 3 raises the cutoff an octave, and each negative volt lowers it one. `limit` keeps it between 20 Hz and 45% of the sample rate, and `port` smooths changes over 20 ms.

### 7. Save it

Save a Synthor preset as usual. It stores the file's path and the encoder positions; loading the preset compiles `filter.csd` again and restores the positions.
