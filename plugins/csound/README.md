# csound

`csound` runs a [Csound 7](https://csound.com) orchestra (`.csd`) on the SSP. The orchestra gets eight audio inputs and outputs, sixteen controls, and MIDI.

Ported from the [`csound` engine](https://github.com/shakfu/sk-engines/tree/main/src/engine/csound) in [sk-engines](https://github.com/shakfu/sk-engines); see [docs/dev/engines.md](../../docs/dev/engines.md).

## Install

Copy `csnd.so` to the `plugins` folder on the SD card. The module is `csnd`: the SSP lists a module only if its id spells its name (see [docs/dev/engines.md](../../docs/dev/engines.md)). Csound is linked in; nothing else is needed. Put orchestras anywhere on the card; the browser opens at `/media/BOOT/csound`.

## Use

Load (button 5) opens a browser. Choose a `.csd` and press Load again. The screen shows the file, and the compile error if there is one. A program that fails to compile leaves the previous one running. With no program, a built-in drone plays: pitch, level and cutoff. MIDI notes play it too.

The preset stores the file's path, not its contents.

## The orchestra

| | |
|-|-|
| controls | `chnget "p1"` .. `chnget "p16"`; see Labels and ranges |
| audio in | `inch 1` .. `inch 8`, or `ins`; `nchnls_i` sets how many |
| audio out | `outch 1` .. `outch 8`, or `outs`; `nchnls` sets how many |
| MIDI | Csound's MIDI opcodes: `massign`, `notnum`, `veloc`, `cpsmidi`, `madsr`, ... |
| files | relative paths (GEN01, `diskin`, `#include`) resolve next to the `.csd` |

- The SSP sets `sr`; the orchestra's own `sr` is ignored.
- `ksmps` is the orchestra's. Output is one k-cycle late.
- Signals are scaled by `0dbfs`: a full-scale SSP signal (1.0) reads as `0dbfs`.
- MIDI comes from the device chosen in the general panel (RS + LS). Notes arrive on channel 1. Csound starts `instr n` for MIDI channel `n` unless told otherwise, so an orchestra that ignores MIDI needs `massign 0, 0`.

## Labels and ranges

A comment line names a control and gives its range; the screen then shows the name, the value in that range and the unit, and the program receives the value in that range:

```
; @p1 cutoff 20 20000 Hz log
; @p2 mix
```

The form is `@pN label [min max [unit]] [log]`, on a line that starts with `;`. The label is one word. Without a range a control is 0 to 1. `log` maps the encoder logarithmically and needs a positive range. The encoder pages show only the declared controls, four to a page, in order; a program that declares none shows all sixteen as `P1`..`P16`. Every control still exists for presets, MIDI learn and rack, and the parameter ids stay `p1`..`p16`; a preset stores the encoder position, so it restores the same position in a program with a different range.

## Examples

`examples/` holds orchestras to copy to the card's `csound/` folder. `make test` compiles and runs each one.

| File | What | Controls |
|-|-|-|
| `filter.csd` | stereo ladder filter on inputs 1-2; input 3 is cutoff CV, 1 V/oct | cutoff, resonance, drive, mix |
| `delay.csd` | stereo ping-pong delay on inputs 1-2 | time, feedback, mix, tone |
| `quadosc.csd` | sine, triangle, saw and pulse on outputs 1-4; input 1 is pitch CV, 1 V/oct | pitch, width |
| `fm-midi.csd` | polyphonic FM synth, played over MIDI; silent until notes arrive from the input chosen in the general panel | ratio, index, attack, release |

## Walkthrough: `filter.csd`

`examples/filter.csd` is a stereo ladder filter. This follows it from the card to the screen.

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

Put `filter.csd` in `csound/` on the card's BOOT partition. The release already has it there.

### 2. Patch it in Synthor

`csnd` always has eight inputs, `In 1` to `In 8`, and eight outputs, `Out 1` to `Out 8`. `nchnls_i = 3` and `nchnls = 2` say how many the orchestra uses:

| In the orchestra | `csnd` jack | Patch |
|-|-|-|
| `inch 1`, `inch 2` | In 1, In 2 | stereo audio to filter |
| `inch 3` | In 3 | cutoff CV, 1 V/oct |
| `outs` | Out 1, Out 2 | to the next module or an output |

### 3. Load it

Press soft key 5 (Load). The browser opens at `/media/BOOT/csound`. Turn encoder 1, or press Up and Down, to select `filter.csd`; Left and Right jump a column, and pressing encoder 1 opens a folder. Press Load again. Soft key 7 (Cancel) leaves without loading.

### 4. Read the screen

```
csnd : Csound 7 host                                  1/1 controls     DSP n.n% pk n.n%
   cutoff       resonance       drive          mix    filter.csd
  40.0 Hz         0.000         1.00          0.000
```

Each `@p` line became one control, in order, four to a row:

| Declaration | Encoder | Screen | Range | The orchestra reads |
|-|-|-|-|-|
| `; @p1 cutoff 40 20000 Hz log` | 1 | `cutoff`, in Hz | 40 to 20000, logarithmic | `chnget "p1"` |
| `; @p2 resonance` | 2 | `resonance` | 0 to 1 | `chnget "p2"` |
| `; @p3 drive 1 9` | 3 | `drive` | 1 to 9 | `chnget "p3"` |
| `; @p4 mix` | 4 | `mix` | 0 to 1 | `chnget "p4"` |

The right panel shows the page (`1/1 controls`), the DSP load, the file name, and a compile error in orange if there is one. Four controls fit one row; a program that declares five to eight gets a second row, and Up and Down move between rows.

### 5. Play it

Every control starts at the bottom of its range, so `mix` is 0 and you hear the dry input. Turn encoder 4 up to hear the filter, then encoder 1 to sweep the cutoff.

- Turning moves a control 5% of its range a detent. `cutoff` is logarithmic, so each detent is the same interval: 40 to 20000 Hz is about 9 octaves, so a detent is about half an octave.
- Holding the encoder while turning moves it 0.5% a detent.
- Pressing and releasing without turning returns it to the bottom of its range.

### 6. Add CV

The orchestra multiplies the cutoff by `2 ^ (In 3 / 0.2)`: each volt on In 3 raises the cutoff an octave, and each negative volt lowers it one. `limit` keeps it between 20 Hz and 45% of the sample rate, and `port` smooths changes over 20 ms.

### 7. Save it

Save a Synthor preset as usual. It stores the file's path and the encoder positions; loading the preset compiles `filter.csd` again and restores the positions.

